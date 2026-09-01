/******************************************************************************
 * link_control.h — CTRL_* frame handlers (docs/PROTOCOL.md §4.3-4.6, §7).
 *
 * link.c calls link_control_dispatch() after a control frame passes framing +
 * CRC + SEQ. Handlers validate the payload, apply changes through the
 * haptic_engine API (staged config, mode change), and reply with STATUS_REP,
 * CTRL_PONG or FAULT via link_tx_*.
 *****************************************************************************/
#ifndef LINK_CONTROL_H
#define LINK_CONTROL_H

#include <stdint.h>

/* @p type is one of TYPE_CTRL_SET_CONFIG / _SET_MODE / _PING / TYPE_STATUS_REQ.
 * @p payload / @p len are the frame payload (already CRC-verified). */
void link_control_dispatch(uint8_t type, const uint8_t *payload, uint8_t len);

#endif /* LINK_CONTROL_H */
