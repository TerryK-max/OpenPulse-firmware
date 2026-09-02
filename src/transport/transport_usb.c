/******************************************************************************
 * transport_usb.c — see transport_usb.h.
 *****************************************************************************/
#include <string.h>

#include "config.h"
#include "transport/transport_usb.h"
#include "link/link.h"
#include "link/proto.h"
#include "usb/usb_vendor.h"
#include "bench/bench_trace.h"

#if TRANSPORT_USB_TX_TRACE
#include "log/log.h"

static void trace_tx(const uint8_t *f, uint16_t n)
{
    uint8_t type = f[PROTO_OFF_TYPE];
    const uint8_t *pl = &f[PROTO_OFF_PAYLOAD];

    if (type == TYPE_STATUS_REP &&
        n >= PROTO_OFF_PAYLOAD + (uint16_t)sizeof(struct stats_msg)) {
        struct stats_msg m; memcpy(&m, pl, sizeof m);
        log_printf("[TX STATUS] up=%lus rx=%lu played=%lu crc=%u gap=%u resync=%u "
                   "bad=%u ovr=%u unr=%u fs=%u fill=%u\r\n",
                   (uint32_t)m.uptime_s, (uint32_t)m.frames_rx,
                   (uint32_t)m.samples_played, m.crc_err, m.seq_gap_frames,
                   m.resync, m.bad_type, m.fifo_overrun, m.fifo_underrun,
                   m.failsafe_trips, m.fifo_fill);
    } else if (type == TYPE_CTRL_PONG &&
               n >= PROTO_OFF_PAYLOAD + (uint16_t)sizeof(struct pong_msg)) {
        struct pong_msg p; memcpy(&p, pl, sizeof p);
        log_printf("[TX PONG] nonce=0x%02X fw=%u.%u proto=%u mode=%u\r\n",
                   p.nonce, p.fw_major, p.fw_minor, p.proto_version, p.mode);
    } else if (type == TYPE_FAULT &&
               n >= PROTO_OFF_PAYLOAD + (uint16_t)sizeof(struct fault_msg)) {
        struct fault_msg fm; memcpy(&fm, pl, sizeof fm);
        log_printf("[TX FAULT] code=%u detail=%lu\r\n", fm.code, (uint32_t)fm.detail);
    } else {
        log_printf("[TX] type=0x%02X len=%u\r\n", type, n);
    }
}
#endif /* TRANSPORT_USB_TX_TRACE */

/* link outbound frame -> vendor bulk IN. Dropped if the queue is full; the PC
 * notices via the STATUS_REP it polls and via SEQ gaps — the protocol is
 * lossy by design (docs/PROTOCOL.md §2). */
static void usb_send_adapter(const uint8_t *frame, uint16_t len)
{
#if TRANSPORT_USB_TX_TRACE
    trace_tx(frame, len);
#endif
    (void)usb_vendor_send(frame, len);
}

/* inbound frame from the pipe -> link. The bench GPIO marker fires here, on
 * DATA frames only, so PA4's edge rate == the frame rate. */
static void tu_rx(const uint8_t *frame, uint16_t len)
{
    if (len >= 1 && frame[PROTO_OFF_TYPE] == TYPE_DATA_SAMPLES)
        bench_trace_rx();
    link_rx(frame, len);
}

static void tu_init(void)
{
    link_init(usb_send_adapter);
    usb_vendor_set_rx(tu_rx);
}

static void    tu_poll(void)        { usb_vendor_poll(); }
static uint8_t tu_host_active(void) { return usb_vendor_host_active(); }

const transport_t transport_usb = { tu_init, tu_poll, tu_host_active };
