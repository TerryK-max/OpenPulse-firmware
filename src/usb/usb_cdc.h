/******************************************************************************
 * usb_cdc.h — the CDC-ACM serial channel of the composite device.
 *
 * Human-readable logs only (docs/PROTOCOL.md §1). Non-blocking: bytes are
 * queued in a ring (USB_LOG_RING_SIZE) and streamed on EP1 IN by the USB ISR;
 * a full ring drops the write. Never call from an ISR (docs/README.md rule 4).
 *
 * The log/ facade forwards here; application code uses log_* not these.
 *****************************************************************************/
#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>

void    usb_cdc_write(const char *s);              /* NUL-terminated */
void    usb_cdc_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint8_t usb_cdc_connected(void);                   /* = usb_device_configured() */

#endif /* USB_CDC_H */
