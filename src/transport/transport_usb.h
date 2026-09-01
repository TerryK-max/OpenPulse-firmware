/******************************************************************************
 * transport_usb.h — USB vendor-bulk transport (docs/ROADMAP.md 3.2).
 *
 * Binds usb_vendor (EP2 OUT/IN) to the link layer. usb_device_init() must have
 * run first. After transport_usb.init():
 *   - host frames on EP2 OUT are delivered to link_rx() from transport_usb.poll()
 *   - link_tx_*() frames go out on EP2 IN
 *****************************************************************************/
#ifndef TRANSPORT_USB_H
#define TRANSPORT_USB_H

#include "transport/transport.h"

extern const transport_t transport_usb;

#endif /* TRANSPORT_USB_H */
