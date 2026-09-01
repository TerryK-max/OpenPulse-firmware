/******************************************************************************
 * usb_vendor.h — the vendor bulk endpoint pair (EP2) of the composite device.
 *
 * Carries whole protocol frames (docs/PROTOCOL.md §1: one USB bulk transfer =
 * one frame, <= 60 bytes). transport_usb binds this to the link layer; nothing
 * above transport_usb ever sees USB.
 *
 * Concurrency (docs/ARCHITECTURE.md §3):
 *   - RX: the USB ISR stages each received packet into a ring (O(1), no
 *     parsing). usb_vendor_poll(), called from the MAIN LOOP, hands the staged
 *     frames to the rx callback in order — so the callback may synthesise
 *     replies with usb_vendor_send() without ISR reentrancy worries.
 *   - TX: usb_vendor_send() (main loop) enqueues a frame; the ISR drains the
 *     queue onto EP2 IN.
 *****************************************************************************/
#ifndef USB_VENDOR_H
#define USB_VENDOR_H

#include <stdint.h>

typedef void (*usb_vendor_rx_fn)(const uint8_t *frame, uint16_t len);

/** Register the inbound-frame handler (transport_usb passes link_rx). */
void usb_vendor_set_rx(usb_vendor_rx_fn cb);

/** Deliver all staged inbound frames to the rx callback. Call every main-loop
 *  iteration. */
void usb_vendor_poll(void);

/** Queue one whole frame for EP2 IN. @return 1 queued, 0 if the queue is full
 *  or the device is not configured. Main-loop only. */
int  usb_vendor_send(const uint8_t *frame, uint16_t len);

/** 1 once the device is configured (vendor endpoints usable). */
uint8_t usb_vendor_ready(void);

/** 1 once at least one frame has arrived from the host — a PC sender is
 *  actually driving the box. Used to hand over from the local self-test. */
uint8_t usb_vendor_host_active(void);

typedef struct {
    uint16_t rx_frames;
    uint16_t rx_overrun;    /* staging ring full -> packet dropped */
    uint16_t rx_oversize;   /* packet > PROTO_MAX_FRAME -> dropped */
    uint16_t tx_frames;
    uint16_t tx_overrun;    /* tx queue full -> frame dropped */
} usb_vendor_stats_t;

const usb_vendor_stats_t *usb_vendor_stats(void);

#endif /* USB_VENDOR_H */
