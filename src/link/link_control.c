/******************************************************************************
 * link_control.c — CTRL_* handlers. See link_control.h and docs/PROTOCOL.md.
 *****************************************************************************/
#include <string.h>

#include "config.h"
#include "link/link_control.h"
#include "link/link.h"
#include "link/proto.h"
#include "haptic/haptic_engine.h"

/* -------------------------------------------------------------------------- */

static void handle_set_config(const uint8_t *payload, uint8_t len)
{
    if (len != (uint8_t)sizeof(struct config_msg)) {
        link_tx_fault(FAULT_BAD_CONFIG, 0);
        return;
    }

    struct config_msg m;
    memcpy(&m, payload, sizeof m);

    /* Version first: a mismatch blocks the data plane until a good config
     * arrives (docs/PROTOCOL.md §7). */
    if (m.version != PROTO_VERSION) {
        link_set_data_blocked(1);
        link_tx_fault(FAULT_VERSION, PROTO_VERSION);
        return;
    }

    /* Validate every field. Apply nothing unless all pass. 0 = "keep" for the
     * unit-bearing fields; loss_policy / flags are taken verbatim. */
    int ok = 1;
    if (m.flags & (uint8_t)~PROTO_CFG_FLAG_MASK)                          ok = 0;
    if (m.ol_lra_period > PROTO_OLP_MAX)                                  ok = 0;
    if (m.sample_rate_hz != 0 &&
        (m.sample_rate_hz < PROTO_RATE_MIN || m.sample_rate_hz > PROTO_RATE_MAX))
                                                                         ok = 0;
    if (m.failsafe_ms != 0 &&
        (m.failsafe_ms < PROTO_FAILSAFE_MIN || m.failsafe_ms > PROTO_FAILSAFE_MAX))
                                                                         ok = 0;
    if (m.amp_max > PROTO_AMP_MAX)                                        ok = 0;
    if (m.loss_policy > 1)                                               ok = 0;
    if (m.reserved != 0)                                                 ok = 0;

    if (!ok) {
        link_tx_fault(FAULT_BAD_CONFIG, 0);
        return;
    }

    haptic_config_t c;
    memset(&c, 0, sizeof c);
    c.ol_lra_period     = m.ol_lra_period;
    c.od_clamp          = m.od_clamp;
    c.sample_rate_hz    = m.sample_rate_hz;
    c.failsafe_ms       = m.failsafe_ms;
    c.amp_max           = m.amp_max;
    c.loss_policy       = m.loss_policy;
    c.underrun_decay_ms = m.underrun_decay_ms;
    haptic_apply_config(&c);          /* staged; haptic_service() applies it */

    link_set_data_blocked(0);         /* version matched -> data plane open */
    /* m.flags PROTO_CFG_FLAG_PERSIST is Phase 6 — accepted here, not acted on. */

    link_tx_status();
}

static void handle_set_mode(const uint8_t *payload, uint8_t len)
{
    if (len != 1) {
        link_tx_fault(FAULT_BAD_MODE, 0xFFu);
        return;
    }

    uint8_t mode = payload[0];
    if (mode > MODE_LOCAL_TEST) {
        link_tx_fault(FAULT_BAD_MODE, mode);
        return;
    }

    /* §7: while the data plane is version-blocked, stay in MODE_IDLE. */
    if (link_data_blocked() && mode != MODE_IDLE) {
        link_tx_fault(FAULT_VERSION, PROTO_VERSION);
        return;
    }

    haptic_set_mode((haptic_mode_t)mode);   /* ramps through 0 */
    link_tx_status();
}

static void handle_ping(const uint8_t *payload, uint8_t len)
{
    link_tx_pong(len >= 1 ? payload[0] : 0);
}

/* -------------------------------------------------------------------------- */

void link_control_dispatch(uint8_t type, const uint8_t *payload, uint8_t len)
{
    switch (type) {
    case TYPE_CTRL_SET_CONFIG: handle_set_config(payload, len); break;
    case TYPE_CTRL_SET_MODE:   handle_set_mode(payload, len);   break;
    case TYPE_CTRL_PING:       handle_ping(payload, len);       break;
    case TYPE_STATUS_REQ:      link_tx_status();                break;
    default:                   break;   /* link.c already filtered the type */
    }
}
