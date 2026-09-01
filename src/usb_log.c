/********************************** (C) COPYRIGHT *******************************
 * File Name  : usb_log.c
 * Description: USB CDC-ACM Virtual Serial Port Logger - Implementation
 *
 * Hardware mapping (CH570D / CH572):
 *
 *  SRAM DMA Buffers (must be 4-byte aligned):
 *    s_ep0_buf[192] -> [EP0: 0..63] [EP4_OUT: 64..127] [EP4_IN: 128..191]
 *    s_ep1_buf[128] -> [EP1_OUT: 0..63] [EP1_IN: 64..127]
 *
 *  USB Endpoints:
 *    EP0          Control   (64B)   - Enumeration / CDC class requests
 *    EP1 OUT      Bulk      (64B)   - Host -> Device data (ignored, auto-ACK)
 *    EP1 IN       Bulk      (64B)   - Device -> Host log data (ring buffer)
 *    EP4 IN       Interrupt (8B)    - CDC SerialState notifications
 *
 *  Interrupt Architecture:
 *    USB_IRQHandler() runs from RAM (__HIGH_CODE) for minimum latency.
 *    It handles:
 *      - SETUP packets: full enumeration state machine
 *      - EP0 IN/OUT: descriptor continuation, SET_LINE_CODING data
 *      - EP1 IN: arms next ring-buffer chunk when previous TX completes
 *      - Bus Reset / Suspend events
 *
 * Copyright (c) 2025 - All Rights Reserved
 *******************************************************************************/

#include "usb_log.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* ==========================================================================
 * Constants
 * ========================================================================== */

#define EP0_SIZE    64u
#define EP1_SIZE    64u
#define EP4_SIZE    8u

#define CDC_SET_LINE_CODING        0x20u
#define CDC_GET_LINE_CODING        0x21u
#define CDC_SET_CONTROL_LINE_STATE 0x22u

/* ==========================================================================
 * DMA Buffers  (4-byte aligned, must reside in internal SRAM)
 * ========================================================================== */

/* EP0 buffer layout:  [EP0: 0..63] [EP4_OUT: 64..127] [EP4_IN: 128..191] */
__attribute__((aligned(4))) static uint8_t s_ep0_buf[64 + 64 + 64];

/* EP1 buffer layout:  [EP1_OUT: 0..63] [EP1_IN: 64..127] */
__attribute__((aligned(4))) static uint8_t s_ep1_buf[64 + 64];

/* ==========================================================================
 * USB CDC-ACM Descriptors
 * ========================================================================== */

/* --- Device Descriptor --------------------------------------------------- */
static const uint8_t s_dev_descr[] =
{
    0x12,                   /* bLength = 18 */
    0x01,                   /* bDescriptorType = DEVICE */
    0x10, 0x01,             /* bcdUSB = 1.10 */
    0x02,                   /* bDeviceClass = CDC Communication */
    0x00,                   /* bDeviceSubClass */
    0x00,                   /* bDeviceProtocol */
    EP0_SIZE,               /* bMaxPacketSize0 = 64 */
    0x86, 0x1A,             /* idVendor  = 0x1A86 (WCH / Qinheng) */
    0x40, 0x80,             /* idProduct = 0x8040 (CDC COM) */
    0x00, 0x01,             /* bcdDevice = 1.00 */
    0x01,                   /* iManufacturer (String 1) */
    0x02,                   /* iProduct      (String 2) */
    0x03,                   /* iSerialNumber (String 3) */
    0x01                    /* bNumConfigurations = 1 */
};

/* --- Configuration Descriptor (total = 67 bytes) ------------------------- */
static const uint8_t s_cfg_descr[] =
{
    /* Configuration Descriptor (9) */
    0x09, 0x02,             /* bLength=9, bDescriptorType=CONFIGURATION */
    0x43, 0x00,             /* wTotalLength = 67 */
    0x02,                   /* bNumInterfaces = 2 */
    0x01,                   /* bConfigurationValue = 1 */
    0x00,                   /* iConfiguration */
    0x80,                   /* bmAttributes: Bus-powered */
    0x32,                   /* MaxPower = 100 mA */

    /* Interface 0: CDC Control (9) */
    0x09, 0x04,
    0x00,                   /* bInterfaceNumber = 0 */
    0x00,                   /* bAlternateSetting = 0 */
    0x01,                   /* bNumEndpoints = 1 (EP4 IN) */
    0x02,                   /* bInterfaceClass = CDC Communication */
    0x02,                   /* bInterfaceSubClass = Abstract Control Model */
    0x01,                   /* bInterfaceProtocol = AT Commands V.250 */
    0x00,                   /* iInterface */

    /* CDC Header Functional Descriptor (5) */
    0x05, 0x24, 0x00, 0x10, 0x01,

    /* CDC ACM Functional Descriptor (4)
       bmCapabilities=0x02: supports Set/Get_Line_Coding,
                            Set_Control_Line_State, Serial_State */
    0x04, 0x24, 0x02, 0x02,

    /* CDC Union Functional Descriptor (5)
       MasterInterface=0, SlaveInterface=1 */
    0x05, 0x24, 0x06, 0x00, 0x01,

    /* CDC Call Management Functional Descriptor (5) */
    0x05, 0x24, 0x01, 0x00, 0x01,

    /* Endpoint 4 IN: Interrupt, 8 bytes, 10 ms interval (7) */
    0x07, 0x05,
    0x84,                   /* bEndpointAddress = EP4 IN */
    0x03,                   /* bmAttributes = Interrupt */
    0x08, 0x00,             /* wMaxPacketSize = 8 */
    0x0A,                   /* bInterval = 10 ms */

    /* Interface 1: CDC Data (9) */
    0x09, 0x04,
    0x01,                   /* bInterfaceNumber = 1 */
    0x00,                   /* bAlternateSetting = 0 */
    0x02,                   /* bNumEndpoints = 2 (EP1 OUT + EP1 IN) */
    0x0A,                   /* bInterfaceClass = CDC Data */
    0x00,                   /* bInterfaceSubClass */
    0x00,                   /* bInterfaceProtocol */
    0x00,                   /* iInterface */

    /* Endpoint 1 OUT: Bulk, 64 bytes (7) */
    0x07, 0x05,
    0x01,                   /* bEndpointAddress = EP1 OUT */
    0x02,                   /* bmAttributes = Bulk */
    0x40, 0x00,             /* wMaxPacketSize = 64 */
    0x00,                   /* bInterval (ignored for Bulk) */

    /* Endpoint 1 IN: Bulk, 64 bytes (7) */
    0x07, 0x05,
    0x81,                   /* bEndpointAddress = EP1 IN */
    0x02,                   /* bmAttributes = Bulk */
    0x40, 0x00,             /* wMaxPacketSize = 64 */
    0x00                    /* bInterval (ignored for Bulk) */
};

/* --- String Descriptors (UTF-16LE, pre-encoded) -------------------------- */
static const uint8_t s_lang_descr[] = { 0x04, 0x03, 0x09, 0x04 }; /* US English */

/* "WCH CH570D" */
static const uint8_t s_mfr_str[] = {
    22, 0x03,
    'W',0, 'C',0, 'H',0, ' ',0, 'C',0, 'H',0, '5',0, '7',0, '0',0, 'D',0
};

/* "USB Logger" */
static const uint8_t s_prod_str[] = {
    22, 0x03,
    'U',0, 'S',0, 'B',0, ' ',0, 'L',0, 'o',0, 'g',0, 'g',0, 'e',0, 'r',0
};

/* "1.0.0" */
static const uint8_t s_ser_str[] = {
    12, 0x03,
    '1',0, '.',0, '0',0, '.',0, '0',0
};

/* --- CDC Line Coding (default: 115200 baud, 8N1) ------------------------- */
/* NOTE: In USB CDC, the actual baud rate is irrelevant for USB transfers.
         It is only meaningful if the CH570D bridges to a physical UART. */
static uint8_t s_line_coding[7] = {
    0x00, 0xC2, 0x01, 0x00,     /* dwDTERate = 115200 bps (little-endian) */
    0x00,                        /* bCharFormat = 1 stop bit */
    0x00,                        /* bParityType = None */
    0x08                         /* bDataBits = 8 */
};

/* ==========================================================================
 * USB Enumeration State
 * ========================================================================== */

static volatile uint8_t  s_configured   = 0;   /* Set to 1 after SET_CONFIGURATION */
static volatile uint8_t  s_ep1_in_busy  = 0;   /* EP1 IN: 1 = transmission in progress */
static volatile uint8_t  s_setup_code   = 0xFF; /* Current SETUP bRequest */
static volatile uint16_t s_setup_len    = 0;   /* Remaining descriptor bytes to send */
static const uint8_t    *s_pDescr       = NULL; /* Pointer into current descriptor */

/* ==========================================================================
 * Log Ring Buffer
 * ========================================================================== */

#define LOG_BUF_MASK  (USB_LOG_BUF_SIZE - 1u)
/* Compile-time check: USB_LOG_BUF_SIZE must be a power of 2 */
_Static_assert((USB_LOG_BUF_SIZE & LOG_BUF_MASK) == 0,
               "USB_LOG_BUF_SIZE must be a power of 2");

static uint8_t           s_log_buf[USB_LOG_BUF_SIZE];
static volatile uint16_t s_log_head = 0; /* Write index (pushed by USB_Log_Print) */
static volatile uint16_t s_log_tail = 0; /* Read index  (drained by USB ISR)      */

/* ==========================================================================
 * Internal: Kick off EP1 IN from ring buffer  (called from ISR and app)
 * ========================================================================== */

/* NOTE: This function must be safe to call from both the ISR context and the
   application context. The s_ep1_in_busy flag prevents concurrent invocations.
   USB_Log_Printf/Print sets the flag only when s_ep1_in_busy==0, and the ISR
   clears it only inside the UIS_TOKEN_IN | 1 case before re-invoking this
   function -- so no explicit critical section is needed on RISC-V (single-core,
   interrupts are the only concurrent actor). */
static void USB_EP1_StartSend(void)
{
    uint16_t avail;
    uint8_t  len;
    uint8_t  i;

    if (s_ep1_in_busy)           return; /* already transmitting */
    if (!s_configured)           return; /* not enumerated yet   */
    if (s_log_head == s_log_tail) return; /* ring buffer empty    */

    /* How many bytes are ready? */
    avail = (uint16_t)((s_log_head - s_log_tail) & LOG_BUF_MASK);
    len   = (avail > EP1_SIZE) ? (uint8_t)EP1_SIZE : (uint8_t)avail;

    /* Copy from ring buffer into EP1 IN DMA buffer */
    for (i = 0; i < len; i++) {
        pEP1_IN_DataBuf[i] = s_log_buf[(s_log_tail + i) & LOG_BUF_MASK];
    }
    s_log_tail = (uint16_t)((s_log_tail + len) & LOG_BUF_MASK);

    /* Arm EP1 IN: set length and change response from NAK -> ACK */
    s_ep1_in_busy = 1;
    DevEP1_IN_Deal(len);
}

/* ==========================================================================
 * USB Transaction Processing  (called from ISR)
 * ========================================================================== */

void USB_DevTransProcess(void)
{
    uint8_t intst  = R8_USB_INT_ST;
    uint8_t len    = 0;
    uint8_t reqtype;

    /* ---- SETUP Packet --------------------------------------------------- */
    if (intst & RB_UIS_SETUP_ACT)
    {
        PUSB_SETUP_REQ pSetup = (PUSB_SETUP_REQ)pEP0_DataBuf;
        s_setup_len  = pSetup->wLength;
        s_setup_code = pSetup->bRequest;
        reqtype      = pSetup->bRequestType;
        len          = 0;

        uint8_t stall = 0; /* Set to 1 to STALL unsupported requests */

        if ((reqtype & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD)
        {
            switch (s_setup_code)
            {
                /* ---- GET_DESCRIPTOR ------------------------------------ */
                case USB_GET_DESCRIPTOR:
                    switch ((uint8_t)(pSetup->wValue >> 8))
                    {
                        case USB_DESCR_TYP_DEVICE:
                            s_pDescr   = s_dev_descr;
                            s_setup_len = (uint16_t)sizeof(s_dev_descr);
                            break;
                        case USB_DESCR_TYP_CONFIG:
                            s_pDescr   = s_cfg_descr;
                            s_setup_len = (uint16_t)sizeof(s_cfg_descr);
                            break;
                        case USB_DESCR_TYP_STRING:
                            switch ((uint8_t)pSetup->wValue)
                            {
                                case 0: s_pDescr = s_lang_descr; s_setup_len = (uint16_t)sizeof(s_lang_descr); break;
                                case 1: s_pDescr = s_mfr_str;    s_setup_len = (uint16_t)sizeof(s_mfr_str);    break;
                                case 2: s_pDescr = s_prod_str;   s_setup_len = (uint16_t)sizeof(s_prod_str);   break;
                                case 3: s_pDescr = s_ser_str;    s_setup_len = (uint16_t)sizeof(s_ser_str);    break;
                                default: stall = 1; break;
                            }
                            break;
                        default:
                            stall = 1;
                            break;
                    }
                    if (!stall)
                    {
                        /* Clamp to host-requested length */
                        if (s_setup_len > pSetup->wLength)
                            s_setup_len = pSetup->wLength;
                        /* Send first chunk */
                        len = (s_setup_len > EP0_SIZE) ? EP0_SIZE : (uint8_t)s_setup_len;
                        memcpy(pEP0_DataBuf, s_pDescr, len);
                        s_pDescr    += len;
                        s_setup_len -= len;
                    }
                    break;

                /* ---- SET_ADDRESS ---------------------------------------- */
                case USB_SET_ADDRESS:
                    /* Save new address; will be applied after status IN stage */
                    s_setup_len = pSetup->wValue & 0x7Fu;
                    len = 0;
                    break;

                /* ---- SET_CONFIGURATION ---------------------------------- */
                case USB_SET_CONFIGURATION:
                    s_configured = (uint8_t)(pSetup->wValue & 0xFF);
                    len = 0;
                    s_setup_len = 0;
                    break;

                /* ---- GET_CONFIGURATION ---------------------------------- */
                case USB_GET_CONFIGURATION:
                    pEP0_DataBuf[0] = s_configured;
                    len = 1;
                    s_setup_len = 0;
                    break;

                /* ---- GET_STATUS ----------------------------------------- */
                case USB_GET_STATUS:
                    pEP0_DataBuf[0] = 0x00;
                    pEP0_DataBuf[1] = 0x00;
                    len = 2;
                    s_setup_len = 0;
                    break;

                /* ---- CLEAR_FEATURE / SET_FEATURE ------------------------ */
                case USB_CLEAR_FEATURE:
                case USB_SET_FEATURE:
                    len = 0;
                    s_setup_len = 0;
                    break;

                /* ---- GET_INTERFACE -------------------------------------- */
                case USB_GET_INTERFACE:
                    pEP0_DataBuf[0] = 0;
                    len = 1;
                    s_setup_len = 0;
                    break;

                /* ---- SET_INTERFACE -------------------------------------- */
                case USB_SET_INTERFACE:
                    len = 0;
                    s_setup_len = 0;
                    break;

                default:
                    stall = 1;
                    break;
            }
        }
        else if ((reqtype & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS)
        {
            /* ---- CDC Class Requests ------------------------------------- */
            switch (s_setup_code)
            {
                case CDC_SET_LINE_CODING:
                    /* Host will send 7 bytes in the OUT data stage */
                    len = 0;
                    break;

                case CDC_GET_LINE_CODING:
                    len = 7;
                    memcpy(pEP0_DataBuf, s_line_coding, 7);
                    s_setup_len = 0;
                    break;

                case CDC_SET_CONTROL_LINE_STATE:
                    /* DTR = bit0, RTS = bit1 of wValue */
                    len = 0;
                    s_setup_len = 0;
                    break;

                default:
                    stall = 1;
                    break;
            }
        }
        else
        {
            stall = 1;
        }

        /* Arm EP0 response */
        if (stall)
        {
            R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG
                         | UEP_R_RES_STALL | UEP_T_RES_STALL;
        }
        else
        {
            R8_UEP0_T_LEN = len;
            R8_UEP0_CTRL  = RB_UEP_R_TOG | RB_UEP_T_TOG
                          | UEP_R_RES_ACK | UEP_T_RES_ACK;
        }
        R8_USB_INT_FG = RB_UIF_TRANSFER;
        return;
    }

    /* ---- Non-SETUP Tokens ----------------------------------------------- */
    uint8_t token = intst & (MASK_UIS_TOKEN | MASK_UIS_ENDP);

    switch (token)
    {
        /* -- EP0 IN: continue sending descriptor or apply SET_ADDRESS -- */
        case UIS_TOKEN_IN:
        {
            if (s_setup_code == USB_SET_ADDRESS)
            {
                /* Apply new device address after status stage */
                R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT)
                               | (uint8_t)s_setup_len;
                s_setup_code = 0xFF;
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;
                break;
            }

            if (s_setup_len == 0)
            {
                /* Status ZLP IN: transaction complete, arm for next SETUP */
                R8_UEP0_T_LEN = 0;
                R8_UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;
            }
            else
            {
                /* Send next chunk of current descriptor */
                len = (s_setup_len > EP0_SIZE) ? EP0_SIZE : (uint8_t)s_setup_len;
                memcpy(pEP0_DataBuf, s_pDescr, len);
                s_pDescr    += len;
                s_setup_len -= len;
                R8_UEP0_T_LEN = len;
                R8_UEP0_CTRL ^= RB_UEP_T_TOG; /* Alternate DATA0 / DATA1 */
            }
            break;
        }

        /* -- EP0 OUT: receive SET_LINE_CODING 7-byte payload ------------ */
        case UIS_TOKEN_OUT:
        {
            if (s_setup_code == CDC_SET_LINE_CODING)
            {
                uint8_t rxlen = R8_USB_RX_LEN;
                if (rxlen >= 7u)
                    memcpy(s_line_coding, pEP0_DataBuf, 7u);
            }
            /* Send zero-length IN status packet */
            R8_UEP0_T_LEN = 0;
            R8_UEP0_CTRL  = RB_UEP_R_TOG | RB_UEP_T_TOG
                          | UEP_R_RES_ACK | UEP_T_RES_ACK;
            break;
        }

        /* -- EP1 IN: previous log chunk was delivered to host ----------- */
        case (UIS_TOKEN_IN | 1u):
        {
            /* Set NAK (no more data yet) and clear busy flag */
            R8_UEP1_CTRL  = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
            s_ep1_in_busy = 0;
            /* Immediately drain next ring-buffer chunk if available */
            USB_EP1_StartSend();
            break;
        }

        /* -- EP1 OUT: data from host (ignored for a TX-only logger) ----- */
        case (UIS_TOKEN_OUT | 1u):
        {
            if (intst & RB_UIS_TOG_OK) {
                /* Auto-toggle is enabled; just flip expected PID */
                R8_UEP1_CTRL ^= RB_UEP_R_TOG;
            }
            break;
        }

        default:
            break;
    }

    R8_USB_INT_FG = RB_UIF_TRANSFER;
}

/* ==========================================================================
 * USB Interrupt Service Routine
 *
 * Runs from SRAM (__HIGH_CODE) for minimum latency.
 * WCH-Interrupt-fast: saves/restores only caller-saved registers.
 * ========================================================================== */
__INTERRUPT
__HIGH_CODE
void USB_IRQHandler(void)
{
    uint8_t intflag = R8_USB_INT_FG;

    /* ---- USB Transfer Complete ------------------------------------------ */
    if (intflag & RB_UIF_TRANSFER)
    {
        USB_DevTransProcess();
        /* RB_UIF_TRANSFER cleared inside USB_DevTransProcess() */
    }

    /* ---- USB Bus Reset -------------------------------------------------- */
    if (intflag & RB_UIF_BUS_RST)
    {
        /* Reset all state */
        s_configured   = 0;
        s_ep1_in_busy  = 0;
        s_setup_code   = 0xFF;
        s_log_tail     = s_log_head; /* Flush ring buffer (host reconnecting) */

        /* Reset device address and endpoint controls */
        R8_USB_DEV_AD = 0x00;
        R8_UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP1_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        R8_UEP4_CTRL  = UEP_T_RES_NAK;

        R8_USB_INT_FG = RB_UIF_BUS_RST;
    }

    /* ---- USB Suspend / Resume ------------------------------------------- */
    if (intflag & RB_UIF_SUSPEND)
    {
        R8_USB_INT_FG = RB_UIF_SUSPEND;
    }
}

/* ==========================================================================
 * Public: USB_Log_Init
 * ========================================================================== */
void USB_Log_Init(void)
{
    /* Point the peripheral driver's extern pointers to our DMA buffers */
    pEP0_RAM_Addr = s_ep0_buf;
    pEP1_RAM_Addr = s_ep1_buf;

    /* Disable JTAG/SWD debug pin that may conflict with USB D+ / D- */
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;

    /* ---- Configure USB controller --------------------------------------- */
    R8_USB_CTRL = 0x00; /* Clear all, start fresh */

    /* Enable EP4 TX (CDC notifications) and EP1 RX+TX (data).
       EP2 and EP3 are not used for CDC logging. */
    R8_UEP4_1_MOD = RB_UEP4_TX_EN | RB_UEP1_RX_EN | RB_UEP1_TX_EN;
    R8_UEP2_3_MOD = 0x00;

    /* Load 16-bit DMA base addresses (SRAM is mapped below 0x10000) */
    R16_UEP0_DMA = (uint16_t)(uint32_t)s_ep0_buf;
    R16_UEP1_DMA = (uint16_t)(uint32_t)s_ep1_buf;

    /* Initial response states */
    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP4_CTRL = UEP_T_RES_NAK;

    /* Reset device address to 0 */
    R8_USB_DEV_AD = 0x00;

    /* Enable USB: device pull-up, auto-NAK while ISR is pending, DMA */
    R8_USB_CTRL = RB_UC_DEV_PU_EN | RB_UC_INT_BUSY | RB_UC_DMA_EN;

    /* Connect USB analog PHY to pins and pull D+ high (signals Full-Speed) */
    R16_PIN_ALTERNATE |= RB_PIN_USB_EN | RB_UDP_PU_EN;

    /* Clear any stale interrupt flags */
    R8_USB_INT_FG = 0xFF;

    /* Enable USB port and disable UDP/UDM internal pull-down resistors */
    R8_UDEV_CTRL = RB_UD_PD_DIS | RB_UD_PORT_EN;

    /* Unmask: Suspend, Bus Reset, Transfer Complete */
    R8_USB_INT_EN = RB_UIE_SUSPEND | RB_UIE_BUS_RST | RB_UIE_TRANSFER;

    /* Enable USB_IRQ in the RISC-V PFIC interrupt controller */
    PFIC_EnableIRQ(USB_IRQn);
}

/* ==========================================================================
 * Public: USB_Log_IsConnected
 * ========================================================================== */
uint8_t USB_Log_IsConnected(void)
{
    return s_configured;
}

/* ==========================================================================
 * Public: USB_Log_Print
 * ========================================================================== */
void USB_Log_Print(const char *str)
{
    uint16_t len;
    uint16_t avail;

    if (!s_configured) return;

    /* Compute string length without strlen (avoids dependency) */
    for (len = 0; str[len] != '\0'; len++);
    if (len == 0u) return;

    /* Compute available ring-buffer space
       (reserve 1 byte so head != tail never means "full") */
    avail = (uint16_t)(USB_LOG_BUF_SIZE - 1u
                       - ((s_log_head - s_log_tail) & LOG_BUF_MASK));

    /* Drop message if ring buffer doesn't have enough room */
    if (len > avail) return;

    /* Push bytes into ring buffer */
    for (uint16_t i = 0; i < len; i++) {
        s_log_buf[s_log_head] = (uint8_t)str[i];
        s_log_head = (uint16_t)((s_log_head + 1u) & LOG_BUF_MASK);
    }

    /* Kick off transmission if EP1 IN is idle */
    USB_EP1_StartSend();
}

/* ==========================================================================
 * Public: USB_Log_Printf
 * ========================================================================== */
void USB_Log_Printf(const char *fmt, ...)
{
    char    buf[256];
    va_list args;
    int     len;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        USB_Log_Print(buf);
    }
}
