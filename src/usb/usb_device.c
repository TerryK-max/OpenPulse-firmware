/******************************************************************************
 * usb_device.c — composite CDC-ACM + vendor-bulk USB device.
 *
 * Evolved from the bench-proven single-interface CDC logger (old src/usb_log.c):
 * the enumeration state machine and the EP0 multi-chunk descriptor sender are
 * kept as-is; the descriptors became composite and EP2 (vendor bulk) was added.
 *
 * See usb_device.h for the endpoint map, usb_cdc.h / usb_vendor.h for the two
 * channel APIs, docs/HARDWARE.md §5 and docs/PROTOCOL.md §1.
 *****************************************************************************/
#include "CH57x_common.h"
#include "CH57x_usbdev.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "config.h"
#include "link/proto.h"
#include "usb/usb_device.h"
#include "usb/usb_cdc.h"
#include "usb/usb_vendor.h"

/* ========================================================================== */
/*  Constants                                                                  */
/* ========================================================================== */

#define EP0_SIZE   64u
#define EP1_SIZE   64u
#define EP2_SIZE   64u

#define CDC_SET_LINE_CODING         0x20u
#define CDC_GET_LINE_CODING         0x21u
#define CDC_SET_CONTROL_LINE_STATE  0x22u

/* ========================================================================== */
/*  DMA buffers  (4-byte aligned, internal SRAM)                               */
/* ========================================================================== */

/* pEP0_RAM_Addr layout: [EP0 0..63][EP4_OUT 64..127][EP4_IN 128..191] */
__attribute__((aligned(4))) static uint8_t s_ep0_buf[64 + 64 + 64];
/* pEP1_RAM_Addr layout: [EP1_OUT 0..63][EP1_IN 64..127] */
__attribute__((aligned(4))) static uint8_t s_ep1_buf[64 + 64];
/* pEP2_RAM_Addr layout: [EP2_OUT 0..63][EP2_IN 64..127] */
__attribute__((aligned(4))) static uint8_t s_ep2_buf[64 + 64];

/* ========================================================================== */
/*  Descriptors                                                                */
/* ========================================================================== */

/* --- Device: IAD composite (class 0xEF / 0x02 / 0x01) -------------------- */
static const uint8_t s_dev_descr[] = {
    0x12, 0x01,             /* bLength 18, DEVICE */
    0x00, 0x02,             /* bcdUSB 2.00 */
    0xEF, 0x02, 0x01,       /* Misc / Common Class / Interface Association Descriptor */
    EP0_SIZE,               /* bMaxPacketSize0 = 64 */
    (uint8_t)USB_VID, (uint8_t)(USB_VID >> 8),
    (uint8_t)USB_PID, (uint8_t)(USB_PID >> 8),
    0x00, 0x03,             /* bcdDevice 3.00 (Phase 3) */
    0x01, 0x02, 0x03,       /* iManufacturer / iProduct / iSerialNumber */
    0x01                    /* bNumConfigurations */
};

/* --- Configuration: 3 interfaces, total 98 bytes ------------------------ */
#define CFG_TOTAL_LEN  98u
static const uint8_t s_cfg_descr[CFG_TOTAL_LEN] = {
    /* Configuration (9) */
    0x09, 0x02, (uint8_t)CFG_TOTAL_LEN, 0x00,
    0x03,                   /* bNumInterfaces = 3 */
    0x01,                   /* bConfigurationValue */
    0x00,                   /* iConfiguration */
    0xA0,                   /* bmAttributes: bus-powered, remote wakeup */
    0x32,                   /* MaxPower = 100 mA */

    /* Interface Association Descriptor (8): interfaces 0..1 = one CDC function */
    0x08, 0x0B,             /* bLength 8, INTERFACE_ASSOCIATION */
    0x00,                   /* bFirstInterface = 0 */
    0x02,                   /* bInterfaceCount = 2 */
    0x02, 0x02, 0x01,       /* CDC / ACM / AT-commands */
    0x00,                   /* iFunction */

    /* Interface 0: CDC Communication, 1 endpoint (9) */
    0x09, 0x04,
    0x00,                   /* bInterfaceNumber = 0 */
    0x00,                   /* bAlternateSetting */
    0x01,                   /* bNumEndpoints = 1 */
    0x02, 0x02, 0x01,       /* CDC / ACM / AT-commands */
    0x00,                   /* iInterface */

    /* CDC functional descriptors */
    0x05, 0x24, 0x00, 0x10, 0x01,          /* Header, bcdCDC 1.10 */
    0x04, 0x24, 0x02, 0x02,                /* ACM, bmCapabilities = Line coding + state */
    0x05, 0x24, 0x06, 0x00, 0x01,          /* Union, master=0 slave=1 */
    0x05, 0x24, 0x01, 0x00, 0x01,          /* Call management, data iface 1 */

    /* EP4 IN: interrupt, 8 bytes, 10 ms — CDC SerialState notification (7) */
    0x07, 0x05, 0x84, 0x03, 0x08, 0x00, 0x0A,

    /* Interface 1: CDC Data, 2 endpoints (9) */
    0x09, 0x04,
    0x01,                   /* bInterfaceNumber = 1 */
    0x00,
    0x02,                   /* bNumEndpoints = 2 */
    0x0A, 0x00, 0x00,       /* CDC Data */
    0x00,

    /* EP1 OUT: bulk, 64 (7) */
    0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00,
    /* EP1 IN: bulk, 64 (7) */
    0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,

    /* Interface 2: vendor specific, 2 endpoints (9) */
    0x09, 0x04,
    0x02,                   /* bInterfaceNumber = 2 */
    0x00,
    0x02,                   /* bNumEndpoints = 2 */
    0xFF, 0x00, 0x00,       /* vendor / none / none */
    0x00,

    /* EP2 OUT: bulk, 64 — protocol frames from host (7) */
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
    /* EP2 IN: bulk, 64 — protocol frames to host (7) */
    0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,
};

/* --- Strings (UTF-16LE, pre-encoded) ----------------------------------- */
static const uint8_t s_lang_descr[] = { 0x04, 0x03, 0x09, 0x04 };   /* en-US */

/* "OpenPulse" */
static const uint8_t s_mfr_str[] = {
    0x14, 0x03,
    'O',0,'p',0,'e',0,'n',0,'P',0,'u',0,'l',0,'s',0,'e',0
};
/* "OpenPulse firmware" */
static const uint8_t s_prod_str[] = {
    0x26, 0x03,
    'O',0,'p',0,'e',0,'n',0,'P',0,'u',0,'l',0,'s',0,'e',0,' ',0,
    'f',0,'i',0,'r',0,'m',0,'w',0,'a',0,'r',0,'e',0
};
/* "0.3.0" */
static const uint8_t s_ser_str[] = {
    0x0C, 0x03,
    '0',0,'.',0,'3',0,'.',0,'0',0
};

/* CDC line coding — cosmetic for a pure-USB CDC (115200 8N1). */
static uint8_t s_line_coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08 };

/* ========================================================================== */
/*  Enumeration state                                                          */
/* ========================================================================== */

static volatile uint8_t  s_configured = 0;
static volatile uint8_t  s_setup_code = 0xFF;
static volatile uint16_t s_setup_len  = 0;
static const uint8_t    *s_pDescr     = NULL;

uint8_t usb_device_configured(void) { return s_configured; }
uint8_t usb_cdc_connected(void)     { return s_configured; }
uint8_t usb_vendor_ready(void)      { return s_configured; }

/* ========================================================================== */
/*  CDC log ring  (EP1 IN)                                                     */
/* ========================================================================== */

#define LOG_MASK  (USB_LOG_RING_SIZE - 1u)
_Static_assert((USB_LOG_RING_SIZE & LOG_MASK) == 0u, "USB_LOG_RING_SIZE must be power of two");

static uint8_t           s_log_buf[USB_LOG_RING_SIZE];
static volatile uint16_t s_log_head = 0;   /* producer: usb_cdc_write */
static volatile uint16_t s_log_tail = 0;   /* consumer: USB ISR */
static volatile uint8_t  s_ep1_in_busy = 0;

static void usb_ep1_kick(void)
{
    uint16_t avail;
    uint8_t  len, i;

    if (s_ep1_in_busy)            return;
    if (!s_configured)            return;
    if (s_log_head == s_log_tail) return;

    avail = (uint16_t)((s_log_head - s_log_tail) & LOG_MASK);
    len   = (avail > EP1_SIZE) ? (uint8_t)EP1_SIZE : (uint8_t)avail;

    for (i = 0; i < len; i++)
        pEP1_IN_DataBuf[i] = s_log_buf[(s_log_tail + i) & LOG_MASK];
    s_log_tail = (uint16_t)((s_log_tail + len) & LOG_MASK);

    s_ep1_in_busy = 1;
    DevEP1_IN_Deal(len);
}

/* ========================================================================== */
/*  Vendor frame rings  (EP2)                                                  */
/* ========================================================================== */

#define VRX_N  USB_VENDOR_RX_DEPTH
#define VTX_N  USB_VENDOR_TX_DEPTH
_Static_assert((VRX_N & (VRX_N - 1u)) == 0u, "USB_VENDOR_RX_DEPTH must be power of two");
_Static_assert((VTX_N & (VTX_N - 1u)) == 0u, "USB_VENDOR_TX_DEPTH must be power of two");

static __attribute__((aligned(4))) uint8_t s_vrx_buf[VRX_N][64];
static volatile uint8_t  s_vrx_len[VRX_N];
static volatile uint32_t s_vrx_head = 0;   /* producer: USB ISR */
static volatile uint32_t s_vrx_tail = 0;   /* consumer: usb_vendor_poll */

static __attribute__((aligned(4))) uint8_t s_vtx_buf[VTX_N][64];
static volatile uint8_t  s_vtx_len[VTX_N];
static volatile uint32_t s_vtx_head = 0;   /* producer: usb_vendor_send */
static volatile uint32_t s_vtx_tail = 0;   /* consumer: usb_ep2_kick */
static volatile uint8_t  s_ep2_in_busy = 0;

static usb_vendor_rx_fn   s_vrx_cb = NULL;
static usb_vendor_stats_t s_vstats;
static volatile uint8_t   s_host_active = 0;

#define BARRIER()  __asm__ volatile("" ::: "memory")

/* ISR context: stage one received packet. O(1), no parsing. */
static void vrx_push(const uint8_t *p, uint8_t n)
{
    uint32_t h = s_vrx_head;
    if (n == 0) return;
    if (n > PROTO_MAX_FRAME) { if (s_vstats.rx_oversize < 0xFFFF) s_vstats.rx_oversize++; return; }
    if ((h - s_vrx_tail) >= VRX_N) { if (s_vstats.rx_overrun < 0xFFFF) s_vstats.rx_overrun++; return; }

    uint32_t i = h & (VRX_N - 1u);
    for (uint8_t k = 0; k < n; k++) s_vrx_buf[i][k] = p[k];
    s_vrx_len[i] = n;
    BARRIER();
    s_vrx_head = h + 1u;
    s_host_active = 1;
    if (s_vstats.rx_frames < 0xFFFF) s_vstats.rx_frames++;
}

/* Main-loop context: start EP2 IN if idle. Guarded against ISR reentry by
 * s_ep2_in_busy (only set here, only cleared in the EP2 IN-done ISR case). */
static void usb_ep2_kick(void)
{
    if (s_ep2_in_busy)            return;
    if (!s_configured)            return;
    if (s_vtx_head == s_vtx_tail) return;

    uint32_t i = s_vtx_tail & (VTX_N - 1u);
    uint8_t  n = s_vtx_len[i];
    for (uint8_t k = 0; k < n; k++) pEP2_IN_DataBuf[k] = s_vtx_buf[i][k];
    s_vtx_tail++;
    s_ep2_in_busy = 1;
    DevEP2_IN_Deal(n);
}

void usb_vendor_set_rx(usb_vendor_rx_fn cb) { s_vrx_cb = cb; }

void usb_vendor_poll(void)
{
    while (s_vrx_head != s_vrx_tail) {
        uint32_t i = s_vrx_tail & (VRX_N - 1u);
        uint8_t  n = s_vrx_len[i];
        if (s_vrx_cb) s_vrx_cb(s_vrx_buf[i], n);
        BARRIER();
        s_vrx_tail++;
    }
}

int usb_vendor_send(const uint8_t *frame, uint16_t len)
{
    if (!s_configured || len == 0u || len > 64u) return 0;

    uint32_t h = s_vtx_head;
    if ((h - s_vtx_tail) >= VTX_N) {
        if (s_vstats.tx_overrun < 0xFFFF) s_vstats.tx_overrun++;
        return 0;
    }
    uint32_t i = h & (VTX_N - 1u);
    for (uint16_t k = 0; k < len; k++) s_vtx_buf[i][k] = frame[k];
    s_vtx_len[i] = (uint8_t)len;
    BARRIER();
    s_vtx_head = h + 1u;
    if (s_vstats.tx_frames < 0xFFFF) s_vstats.tx_frames++;

    usb_ep2_kick();
    return 1;
}

uint8_t usb_vendor_host_active(void)         { return s_host_active; }
const usb_vendor_stats_t *usb_vendor_stats(void) { return &s_vstats; }

/* ========================================================================== */
/*  USB transaction processing  (ISR)                                          */
/* ========================================================================== */

void USB_DevTransProcess(void)
{
    uint8_t intst = R8_USB_INT_ST;
    uint8_t len   = 0;
    uint8_t reqtype;

    /* ---- SETUP ---- */
    if (intst & RB_UIS_SETUP_ACT)
    {
        PUSB_SETUP_REQ pS = (PUSB_SETUP_REQ)pEP0_DataBuf;
        s_setup_len  = pS->wLength;
        s_setup_code = pS->bRequest;
        reqtype      = pS->bRequestType;
        len          = 0;

        uint8_t stall = 0;

        if ((reqtype & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD)
        {
            switch (s_setup_code)
            {
            case USB_GET_DESCRIPTOR:
                switch ((uint8_t)(pS->wValue >> 8))
                {
                case USB_DESCR_TYP_DEVICE:
                    s_pDescr = s_dev_descr; s_setup_len = (uint16_t)sizeof(s_dev_descr);
                    break;
                case USB_DESCR_TYP_CONFIG:
                    s_pDescr = s_cfg_descr; s_setup_len = (uint16_t)sizeof(s_cfg_descr);
                    break;
                case USB_DESCR_TYP_STRING:
                    switch ((uint8_t)pS->wValue) {
                    case 0: s_pDescr = s_lang_descr; s_setup_len = sizeof(s_lang_descr); break;
                    case 1: s_pDescr = s_mfr_str;    s_setup_len = sizeof(s_mfr_str);    break;
                    case 2: s_pDescr = s_prod_str;   s_setup_len = sizeof(s_prod_str);   break;
                    case 3: s_pDescr = s_ser_str;    s_setup_len = sizeof(s_ser_str);    break;
                    default: stall = 1; break;
                    }
                    break;
                default:
                    stall = 1;
                    break;
                }
                if (!stall) {
                    if (s_setup_len > pS->wLength) s_setup_len = pS->wLength;
                    len = (s_setup_len > EP0_SIZE) ? EP0_SIZE : (uint8_t)s_setup_len;
                    memcpy(pEP0_DataBuf, s_pDescr, len);
                    s_pDescr += len; s_setup_len -= len;
                }
                break;

            case USB_SET_ADDRESS:
                s_setup_len = pS->wValue & 0x7Fu; len = 0;
                break;
            case USB_SET_CONFIGURATION:
                s_configured = (uint8_t)(pS->wValue & 0xFF); len = 0; s_setup_len = 0;
                break;
            case USB_GET_CONFIGURATION:
                pEP0_DataBuf[0] = s_configured; len = 1; s_setup_len = 0;
                break;
            case USB_GET_STATUS:
                pEP0_DataBuf[0] = 0x00; pEP0_DataBuf[1] = 0x00; len = 2; s_setup_len = 0;
                break;
            case USB_CLEAR_FEATURE:
            case USB_SET_FEATURE:
                len = 0; s_setup_len = 0;
                break;
            case USB_GET_INTERFACE:
                pEP0_DataBuf[0] = 0; len = 1; s_setup_len = 0;
                break;
            case USB_SET_INTERFACE:
                len = 0; s_setup_len = 0;
                break;
            default:
                stall = 1;
                break;
            }
        }
        else if ((reqtype & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS)
        {
            /* CDC class requests (interface 0). */
            switch (s_setup_code)
            {
            case CDC_SET_LINE_CODING:        len = 0; break;   /* data stage follows */
            case CDC_GET_LINE_CODING:
                len = 7; memcpy(pEP0_DataBuf, s_line_coding, 7); s_setup_len = 0;
                break;
            case CDC_SET_CONTROL_LINE_STATE: len = 0; s_setup_len = 0; break;
            default:                         stall = 1; break;
            }
        }
        else
        {
            stall = 1;   /* vendor control requests: none until MS OS 2.0 (Phase 3.6) */
        }

        if (stall) {
            R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
        } else {
            R8_UEP0_T_LEN = len;
            R8_UEP0_CTRL  = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
        }
        R8_USB_INT_FG = RB_UIF_TRANSFER;
        return;
    }

    /* ---- non-SETUP tokens ---- */
    switch (intst & (MASK_UIS_TOKEN | MASK_UIS_ENDP))
    {
    case UIS_TOKEN_IN:                 /* EP0 IN: descriptor continuation / SET_ADDRESS */
        if (s_setup_code == USB_SET_ADDRESS) {
            R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | (uint8_t)s_setup_len;
            s_setup_code = 0xFF;
            R8_UEP0_T_LEN = 0;
            R8_UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;
        } else if (s_setup_len == 0) {
            R8_UEP0_T_LEN = 0;
            R8_UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;
        } else {
            len = (s_setup_len > EP0_SIZE) ? EP0_SIZE : (uint8_t)s_setup_len;
            memcpy(pEP0_DataBuf, s_pDescr, len);
            s_pDescr += len; s_setup_len -= len;
            R8_UEP0_T_LEN = len;
            R8_UEP0_CTRL ^= RB_UEP_T_TOG;
        }
        break;

    case UIS_TOKEN_OUT:               /* EP0 OUT: SET_LINE_CODING payload */
        if (s_setup_code == CDC_SET_LINE_CODING && R8_USB_RX_LEN >= 7u)
            memcpy(s_line_coding, pEP0_DataBuf, 7u);
        R8_UEP0_T_LEN = 0;
        R8_UEP0_CTRL  = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
        break;

    case (UIS_TOKEN_IN | 1u):         /* EP1 IN: CDC log chunk delivered */
        R8_UEP1_CTRL  = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
        s_ep1_in_busy = 0;
        usb_ep1_kick();
        break;

    case (UIS_TOKEN_OUT | 1u):        /* EP1 OUT: CDC text from host — drained */
        if (intst & RB_UIS_TOG_OK)
            R8_UEP1_CTRL ^= RB_UEP_R_TOG;
        break;

    case (UIS_TOKEN_OUT | 2u):        /* EP2 OUT: one protocol frame from host */
        if (intst & RB_UIS_TOG_OK) {
            vrx_push(pEP2_OUT_DataBuf, R8_USB_RX_LEN);
            R8_UEP2_CTRL ^= RB_UEP_R_TOG;
        }
        break;

    case (UIS_TOKEN_IN | 2u):         /* EP2 IN: one protocol frame delivered */
        R8_UEP2_CTRL  = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
        s_ep2_in_busy = 0;
        usb_ep2_kick();
        break;

    default:
        break;
    }

    R8_USB_INT_FG = RB_UIF_TRANSFER;
}

__INTERRUPT __HIGH_CODE
void USB_IRQHandler(void)
{
    uint8_t f = R8_USB_INT_FG;

    if (f & RB_UIF_TRANSFER) {
        USB_DevTransProcess();               /* clears RB_UIF_TRANSFER itself */
    }

    if (f & RB_UIF_BUS_RST) {
        s_configured  = 0;
        s_setup_code  = 0xFF;
        s_ep1_in_busy = 0;
        s_ep2_in_busy = 0;
        s_log_tail    = s_log_head;           /* flush pending logs on reconnect */
        s_vrx_tail    = s_vrx_head;
        s_vtx_tail    = s_vtx_head;

        R8_USB_DEV_AD = 0x00;
        R8_UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP1_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        R8_UEP2_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        R8_UEP4_CTRL  = UEP_T_RES_NAK;

        R8_USB_INT_FG = RB_UIF_BUS_RST;
    }

    if (f & RB_UIF_SUSPEND)
        R8_USB_INT_FG = RB_UIF_SUSPEND;
}

/* ========================================================================== */
/*  Init                                                                       */
/* ========================================================================== */

void usb_device_init(void)
{
    pEP0_RAM_Addr = s_ep0_buf;
    pEP1_RAM_Addr = s_ep1_buf;
    pEP2_RAM_Addr = s_ep2_buf;

    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;

    R8_USB_CTRL = 0x00;

    /* EP4 TX (CDC notify), EP1 RX+TX (CDC data), EP2 RX+TX (vendor bulk). */
    R8_UEP4_1_MOD = RB_UEP4_TX_EN | RB_UEP1_RX_EN | RB_UEP1_TX_EN;
    R8_UEP2_3_MOD = RB_UEP2_RX_EN | RB_UEP2_TX_EN;

    R16_UEP0_DMA = (uint16_t)(uint32_t)s_ep0_buf;
    R16_UEP1_DMA = (uint16_t)(uint32_t)s_ep1_buf;
    R16_UEP2_DMA = (uint16_t)(uint32_t)s_ep2_buf;

    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP4_CTRL = UEP_T_RES_NAK;

    R8_USB_DEV_AD = 0x00;
    R8_USB_CTRL   = RB_UC_DEV_PU_EN | RB_UC_INT_BUSY | RB_UC_DMA_EN;
    R16_PIN_ALTERNATE |= RB_PIN_USB_EN | RB_UDP_PU_EN;
    R8_USB_INT_FG = 0xFF;
    R8_UDEV_CTRL  = RB_UD_PD_DIS | RB_UD_PORT_EN;
    R8_USB_INT_EN = RB_UIE_SUSPEND | RB_UIE_BUS_RST | RB_UIE_TRANSFER;

    PFIC_EnableIRQ(USB_IRQn);
}

/* ========================================================================== */
/*  usb_cdc.h implementation                                                   */
/* ========================================================================== */

void usb_cdc_write(const char *str)
{
    uint16_t len, avail, i;

    if (!s_configured || str == NULL) return;
    for (len = 0; str[len] != '\0'; len++) { }
    if (len == 0u) return;

    avail = (uint16_t)(USB_LOG_RING_SIZE - 1u - ((s_log_head - s_log_tail) & LOG_MASK));
    if (len > avail) return;                  /* drop — logs are best-effort */

    for (i = 0; i < len; i++) {
        s_log_buf[s_log_head] = (uint8_t)str[i];
        s_log_head = (uint16_t)((s_log_head + 1u) & LOG_MASK);
    }
    usb_ep1_kick();
}

void usb_cdc_printf(const char *fmt, ...)
{
    char    buf[256];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (n > 0) usb_cdc_write(buf);
}
