/********************************** (C) COPYRIGHT *******************************
 * File Name  : usb_log.h
 * Description: USB CDC-ACM Virtual Serial Port Logger
 *              Presents the CH570D as a USB serial port to the host OS.
 *              No custom driver needed (uses built-in CDC-ACM on Win/Mac/Linux).
 *
 * Usage (host side):
 *   - Windows : Open the COM port with any terminal (PuTTY, TeraTerm...)
 *   - macOS   : screen /dev/tty.usbmodem*  115200
 *   - Linux   : screen /dev/ttyACM0  115200
 *
 * Architecture:
 *   - EP0     : USB Control (enumeration, CDC class requests)
 *   - EP1 IN  : Bulk TX  (64 bytes/packet, log data -> host)
 *   - EP1 OUT : Bulk RX  (64 bytes/packet, host -> device, ignored)
 *   - EP4 IN  : Interrupt (8 bytes/packet, CDC SerialState notification)
 *
 *   Log data flows through a ring buffer (USB_LOG_BUF_SIZE bytes).
 *   The USB ISR drains the ring buffer automatically over EP1 IN.
 *******************************************************************************/

#ifndef USB_LOG_H
#define USB_LOG_H

#include "CH57x_common.h"
#include "CH57x_usbdev.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------------- */

/** Ring buffer size in bytes. MUST be a power of 2. */
#define USB_LOG_BUF_SIZE    512u

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * @brief  Initialize the USB CDC-ACM peripheral.
 *         Call once after SetSysClock().
 * @note   Disables the debug pin (RB_PIN_DEBUG_EN) to free USB lines.
 */
void USB_Log_Init(void);

/**
 * @brief  Write a null-terminated string to the USB log stream.
 *         Data is silently dropped when the ring buffer is full.
 * @param  str  Null-terminated ASCII string.
 */
void USB_Log_Print(const char *str);

/**
 * @brief  Write a formatted string to the USB log stream (printf-style).
 *         Uses an internal 256-byte stack buffer for formatting.
 * @param  fmt  printf-style format string.
 */
void USB_Log_Printf(const char *fmt, ...);

/**
 * @brief  Return 1 if the host has completed USB enumeration, 0 otherwise.
 */
uint8_t USB_Log_IsConnected(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_LOG_H */
