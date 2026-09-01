/******************************************************************************
 * link.h — transport-agnostic protocol layer (docs/ARCHITECTURE.md §2 layer 4,
 * docs/PROTOCOL.md).
 *
 * Bytes in  -> validated frame -> dispatch (DATA -> FIFO, ENVELOPE -> engine,
 *              CTRL_* -> link_control).
 * Events out -> framed bytes -> injected transport sink.
 *
 * NO knowledge of USB / RF / CDC. The unit test in tools/test/ drives it with
 * crafted byte arrays and a capturing sink. Runs entirely in the main loop —
 * never from an ISR, never blocks.
 *****************************************************************************/
#ifndef LINK_H
#define LINK_H

#include <stdint.h>
#include "link/proto.h"

/* Outbound frame sink, injected by the transport (Phase 3). While it is NULL
 * link still builds frames and updates counters, then drops them. */
typedef void (*link_send_fn)(const uint8_t *frame, uint16_t len);

void link_init(link_send_fn send);

/**
 * Feed one received delivery unit (exactly one whole frame — docs/PROTOCOL.md
 * §1). Checks LEN, CRC8 and SEQ continuity, then dispatches by TYPE. Bad frames
 * are dropped silently (counters only). Never blocks, never retransmits.
 */
void link_rx(const uint8_t *frame, uint16_t len);

/* ---- outbound builders (link_control and the main loop call these) ------ */
void link_tx_pong(uint8_t nonce);
void link_tx_status(void);
void link_tx_log(uint8_t level, const char *text);
void link_tx_fault(uint8_t code, uint32_t detail);

/* ---- version gate (docs/PROTOCOL.md §7) -------------------------------- */
/* While set, DATA_SAMPLES / ENVELOPE frames are ignored and the box stays in
 * MODE_IDLE. Set on a CTRL_SET_CONFIG version mismatch, cleared on a matching
 * one. link_control owns the transitions. */
uint8_t link_data_blocked(void);
void    link_set_data_blocked(uint8_t blocked);

#endif /* LINK_H */
