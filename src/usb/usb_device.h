/******************************************************************************
 * usb_device.h — composite USB device (docs/ARCHITECTURE.md §2 layer 5).
 *
 *   Interface 0+1 : CDC-ACM  — human-readable logs (+ a text control fallback).
 *                              Host sees a /dev/cu.usbmodem* serial port.
 *   Interface 2   : vendor   — a bulk OUT/IN pair (EP2) carrying the framed
 *                              protocol (docs/PROTOCOL.md). Claimed by libusb
 *                              on the PC; no OS driver, no .inf.
 *
 * Owns the USB peripheral, its DMA buffers and USB_IRQHandler. The CDC side is
 * driven through usb_cdc.h, the vendor side through usb_vendor.h.
 *
 * Endpoint map (CH570D has EP0..EP4, each bidirectional):
 *   EP0      control     64   enumeration + CDC class requests
 *   EP1 IN   bulk        64   CDC data  -> host   (log stream)
 *   EP1 OUT  bulk        64   CDC data  <- host   (text fallback; drained)
 *   EP2 OUT  bulk        64   vendor    <- host   (protocol frames -> link_rx)
 *   EP2 IN   bulk        64   vendor    -> host   (protocol frames from link)
 *   EP4 IN   interrupt    8   CDC SerialState notification (declared, NAK'd)
 *   EP3            —           free (RF bridge, Phase 4)
 *****************************************************************************/
#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <stdint.h>

/** Bring up the composite device. Call once after board_init(). */
void    usb_device_init(void);

/** 1 once the host has issued SET_CONFIGURATION (enumeration complete). */
uint8_t usb_device_configured(void);

#endif /* USB_DEVICE_H */
