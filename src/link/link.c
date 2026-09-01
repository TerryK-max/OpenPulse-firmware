/******************************************************************************
 * link.c — protocol framing / CRC / sequence / dispatch. See link.h.
 *
 * docs/PROTOCOL.md is the contract. This file has zero transport knowledge:
 * link_rx() is fed whole frames, link_tx_*() hand whole frames to an injected
 * sink. Everything runs in the main loop.
 *****************************************************************************/
#include <string.h>

#include "config.h"
#include "link/link.h"
#include "link/link_control.h"
#include "link/proto.h"
#include "util/crc.h"
#include "haptic/haptic_engine.h"
#include "haptic/haptic_fifo.h"

static link_send_fn s_send;

static uint8_t s_rx_seq;          /* last accepted PC->box SEQ */
static uint8_t s_have_rx_seq;     /* 0 until the first frame reseats it */
static uint8_t s_tx_seq;          /* next box->PC SEQ */
static uint8_t s_data_blocked;    /* docs/PROTOCOL.md §7 */

static int8_t  s_last_sample;     /* for the hold-last loss policy */
static uint8_t s_last_data_len = 1;   /* recent DATA payload size, for loss estimate */

/* -------------------------------------------------------------------------- */

void link_init(link_send_fn send)
{
    s_send         = send;
    s_rx_seq       = 0;
    s_have_rx_seq  = 0;
    s_tx_seq       = 0;
    s_data_blocked = 0;
    s_last_sample  = 0;
    s_last_data_len = 1;
}

uint8_t link_data_blocked(void)          { return s_data_blocked; }
void    link_set_data_blocked(uint8_t b) { s_data_blocked = b ? 1u : 0u; }

/* -------------------------------------------------------------------------- */
/*  outbound                                                                  */
/* -------------------------------------------------------------------------- */

static void tx_frame(uint8_t type, const uint8_t *payload, uint8_t len)
{
    uint8_t f[PROTO_MAX_FRAME];

    if (len > PROTO_MAX_PAYLOAD) return;          /* our own frames never exceed this */

    f[PROTO_OFF_TYPE] = type;
    f[PROTO_OFF_SEQ]  = s_tx_seq++;
    f[PROTO_OFF_LEN]  = len;
    if (len) memcpy(&f[PROTO_OFF_PAYLOAD], payload, len);

    uint16_t body = (uint16_t)(PROTO_OFF_PAYLOAD + len);
    f[body] = crc8_smbus(f, body);

    if (s_send) s_send(f, (uint16_t)(body + 1u));
}

void link_tx_pong(uint8_t nonce)
{
    struct pong_msg p;
    p.nonce         = nonce;
    p.fw_major      = FW_VERSION_MAJOR;
    p.fw_minor      = FW_VERSION_MINOR;
    p.proto_version = PROTO_VERSION;
    p.mode          = (uint8_t)haptic_get_mode();
    tx_frame(TYPE_CTRL_PONG, (const uint8_t *)&p, (uint8_t)sizeof p);
}

void link_tx_status(void)
{
    const haptic_stats_t *s = haptic_stats();
    struct stats_msg m;

    memset(&m, 0, sizeof m);
    m.fw_major         = FW_VERSION_MAJOR;
    m.fw_minor         = FW_VERSION_MINOR;
    m.proto_version    = PROTO_VERSION;
    m.mode             = (uint8_t)haptic_get_mode();
    m.uptime_s         = haptic_now_ms() / 1000u;
    m.frames_rx        = s->frames_rx;
    m.samples_played   = s->samples_played;
    m.crc_err          = s->crc_err;
    m.bad_type         = s->bad_type;
    m.seq_gap_frames   = s->seq_gap_frames;
    m.resync           = s->resync;
    m.fifo_overrun     = s->fifo_overrun;
    m.fifo_underrun    = s->fifo_underrun;
    m.i2c_err          = s->i2c_err;
    m.failsafe_trips   = s->failsafe_trips;
    m.tick_backlog_max = s->tick_backlog_max;
    m.fifo_fill        = (uint16_t)haptic_fifo_count(haptic_fifo());
    m.drv_status       = s->drv_status;
    m.drv_vbat         = s->drv_vbat;

    tx_frame(TYPE_STATUS_REP, (const uint8_t *)&m, (uint8_t)sizeof m);
}

void link_tx_log(uint8_t level, const char *text)
{
    uint8_t  buf[1 + PROTO_LOG_MAX_TEXT];
    uint16_t n = 0;

    buf[n++] = level;
    if (text)
        while (*text && n < (uint16_t)(1 + PROTO_LOG_MAX_TEXT))
            buf[n++] = (uint8_t)*text++;

    tx_frame(TYPE_LOG, buf, (uint8_t)n);
}

void link_tx_fault(uint8_t code, uint32_t detail)
{
    struct fault_msg m;
    m.code   = code;
    m.detail = detail;
    tx_frame(TYPE_FAULT, (const uint8_t *)&m, (uint8_t)sizeof m);
}

/* -------------------------------------------------------------------------- */
/*  inbound                                                                   */
/* -------------------------------------------------------------------------- */

static void bump16(uint16_t *c)          { if (*c < 0xFFFFu) (*c)++; }
static void add16 (uint16_t *c, uint16_t n)
{
    uint32_t v = (uint32_t)*c + n;
    *c = (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v;
}

static void do_resync(uint8_t seq)
{
    haptic_abort();                       /* drain FIFO, envelope target 0, ramp to 0 */
    s_rx_seq      = seq;
    s_have_rx_seq = 1;
    bump16(&haptic_stats()->resync);
}

/* Insert up to n placeholder samples for a detected DATA gap (docs/PROTOCOL.md
 * §4.1). Capped so a bogus estimate cannot stall the loop. */
static void fill_lost_samples(uint16_t n)
{
    if (n > HAPTIC_FIFO_CAP) n = HAPTIC_FIFO_CAP;
    int8_t v = (haptic_config()->loss_policy == 1) ? s_last_sample : (int8_t)0;
    for (uint16_t i = 0; i < n; i++)
        if (!haptic_push_sample(v)) break;
}

void link_rx(const uint8_t *frame, uint16_t len)
{
    haptic_stats_t *st = haptic_stats();

    /* -- framing + integrity -- */
    if (len < PROTO_MIN_FRAME || len > PROTO_MAX_FRAME) { bump16(&st->crc_err); return; }

    uint8_t type = frame[PROTO_OFF_TYPE];
    uint8_t seq  = frame[PROTO_OFF_SEQ];
    uint8_t plen = frame[PROTO_OFF_LEN];

    if ((uint16_t)plen + PROTO_FRAME_OVERHEAD != len) { bump16(&st->crc_err); return; }

    uint16_t body = (uint16_t)(PROTO_OFF_PAYLOAD + plen);
    if (crc8_smbus(frame, body) != frame[body]) { bump16(&st->crc_err); return; }

    const uint8_t *payload = &frame[PROTO_OFF_PAYLOAD];

    if (st->frames_rx != 0xFFFFFFFFu) st->frames_rx++;

    /* -- RESYNC is handled before SEQ accounting (it reseats the counter) -- */
    if (type == TYPE_RESYNC) { do_resync(seq); return; }

    /* -- SEQ / gap accounting -- */
    if (!s_have_rx_seq) {
        s_have_rx_seq = 1;
    } else {
        uint8_t gap = (uint8_t)(seq - s_rx_seq - 1u);
        if (gap == 0) {
            /* in order */
        } else if (gap < PROTO_RESYNC_GAP) {
            add16(&st->seq_gap_frames, gap);
            if (type == TYPE_DATA_SAMPLES && !s_data_blocked)
                fill_lost_samples((uint16_t)gap * s_last_data_len);
        } else {
            do_resync(seq);              /* big gap: flush + reseat, then still take this frame */
        }
    }
    s_rx_seq = seq;

    /* -- dispatch -- */
    switch (type) {
    case TYPE_DATA_SAMPLES:
        if (s_data_blocked) break;               /* §7: ignored until version matches */
        if (plen) {
            haptic_push_samples((const int8_t *)payload, plen);
            s_last_sample   = (int8_t)payload[plen - 1u];
            s_last_data_len = plen;
        }
        haptic_notify_data();
        break;

    case TYPE_ENVELOPE:
        if (s_data_blocked) break;
        if (plen != (uint8_t)sizeof(struct env_cmd)) { bump16(&st->crc_err); break; }
        {
            struct env_cmd e;
            memcpy(&e, payload, sizeof e);
            haptic_set_envelope(e.target, e.slew_ms);
        }
        haptic_notify_data();
        break;

    case TYPE_CTRL_SET_CONFIG:
    case TYPE_CTRL_SET_MODE:
    case TYPE_CTRL_PING:
    case TYPE_STATUS_REQ:
        link_control_dispatch(type, payload, plen);
        break;

    default:
        bump16(&st->bad_type);
        if ((st->bad_type & 0x3Fu) == 1u)         /* rate-limited WARN */
            link_tx_log(PROTO_LOG_WARN, "link: unknown frame type");
        break;
    }
}
