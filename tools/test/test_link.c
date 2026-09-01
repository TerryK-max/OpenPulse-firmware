/******************************************************************************
 * test_link.c — host unit tests for src/link/ (Phase 2.5).
 *
 * Feeds crafted frames into link_rx() with a capturing transport sink and
 * asserts FIFO contents / engine calls / reply frames. No target, no I/O.
 *
 *   cd tools/test && make            # build + run
 *****************************************************************************/
#include <stdio.h>
#include <string.h>

#include "link/link.h"
#include "link/proto.h"
#include "util/crc.h"
#include "haptic/haptic_engine.h"
#include "mock_engine.h"

/* ---- tiny assert harness ---------------------------------------------- */
static int g_checks, g_fails;
#define CHECK(cond, ...) do {                                              \
        g_checks++;                                                        \
        if (!(cond)) { g_fails++;                                          \
            printf("  FAIL %s:%d: ", __func__, __LINE__);                  \
            printf(__VA_ARGS__); printf("\n"); }                          \
    } while (0)

/* ---- captured outbound frames --------------------------------------- */
#define CAP_MAX 32
static uint8_t  cap_buf[CAP_MAX][PROTO_MAX_FRAME];
static uint16_t cap_len[CAP_MAX];
static int      cap_n;

static void capture(const uint8_t *f, uint16_t n)
{
    if (cap_n < CAP_MAX) { memcpy(cap_buf[cap_n], f, n); cap_len[cap_n] = n; }
    cap_n++;
}

static void setup(void)
{
    haptic_engine_init();
    link_init(capture);
    cap_n = 0;
}

/* ---- frame builder --------------------------------------------------- */
static uint16_t build(uint8_t *out, uint8_t type, uint8_t seq,
                      const void *pl, uint8_t len)
{
    out[PROTO_OFF_TYPE] = type;
    out[PROTO_OFF_SEQ]  = seq;
    out[PROTO_OFF_LEN]  = len;
    if (len && pl) memcpy(out + PROTO_OFF_PAYLOAD, pl, len);
    out[PROTO_OFF_PAYLOAD + len] = crc8_smbus(out, PROTO_OFF_PAYLOAD + len);
    return (uint16_t)(PROTO_OFF_PAYLOAD + len + 1);
}

static void rx(uint8_t type, uint8_t seq, const void *pl, uint8_t len)
{
    uint8_t f[PROTO_MAX_FRAME + 8];
    uint16_t n = build(f, type, seq, pl, len);
    link_rx(f, n);
}

/* Find the first captured frame of a given type, -1 if none. */
static int cap_find(uint8_t type)
{
    for (int i = 0; i < cap_n && i < CAP_MAX; i++)
        if (cap_buf[i][PROTO_OFF_TYPE] == type) return i;
    return -1;
}

/* ==================================================================== */

static void t_crc_vector(void)
{
    const uint8_t s[] = "123456789";
    CHECK(crc8_smbus(s, 9) == 0xF4, "CRC-8/SMBUS check value should be 0xF4");
    CHECK(proto_crc8(s, 9) == 0xF4, "proto_crc8 must match crc8_smbus");
}

static void t_struct_sizes(void)
{
    CHECK(sizeof(struct config_msg) == 12, "config_msg wire size");
    CHECK(sizeof(struct stats_msg)  == 40, "stats_msg wire size");
    CHECK(sizeof(struct pong_msg)   ==  5, "pong_msg wire size");
    CHECK(sizeof(struct env_cmd)    ==  2, "env_cmd wire size");
    CHECK(sizeof(struct fault_msg)  ==  5, "fault_msg wire size");
}

static void t_data_samples_ok(void)
{
    setup();
    int8_t s[4] = { 10, -20, 30, 127 };
    rx(TYPE_DATA_SAMPLES, 0, s, 4);

    int8_t got[16];
    uint32_t n = mock_drain_fifo(got, 16);
    CHECK(n == 4, "4 samples buffered, got %u", n);
    CHECK(memcmp(got, s, 4) == 0, "sample bytes preserved in order");
    CHECK(g_mock.notify_data_calls == 1, "notify_data called once");
    CHECK(haptic_stats()->frames_rx == 1, "frames_rx == 1");
    CHECK(haptic_stats()->crc_err == 0, "no crc errors");
}

static void t_bad_crc(void)
{
    setup();
    uint8_t f[PROTO_MAX_FRAME];
    int8_t s[2] = { 1, 2 };
    uint16_t n = build(f, TYPE_DATA_SAMPLES, 0, s, 2);
    f[n - 1] ^= 0xFF;                       /* corrupt the CRC */
    link_rx(f, n);

    int8_t got[8];
    CHECK(mock_drain_fifo(got, 8) == 0, "corrupt frame dropped, FIFO empty");
    CHECK(haptic_stats()->crc_err == 1, "crc_err == 1");
    CHECK(cap_n == 0, "no reply to a corrupt frame");
}

static void t_short_and_oversize(void)
{
    setup();
    uint8_t f[PROTO_MAX_FRAME + 8];

    link_rx(f, 3);                                  /* below PROTO_MIN_FRAME */
    CHECK(haptic_stats()->crc_err == 1, "short frame -> crc_err");

    memset(f, 0, sizeof f);
    f[PROTO_OFF_TYPE] = TYPE_DATA_SAMPLES;
    f[PROTO_OFF_LEN]  = 57;                         /* > PROTO_MAX_PAYLOAD */
    link_rx(f, 57 + PROTO_FRAME_OVERHEAD);          /* = 61 > PROTO_MAX_FRAME */
    CHECK(haptic_stats()->crc_err == 2, "oversize frame -> crc_err");

    /* LEN field inconsistent with the delivered length */
    int8_t s[4] = { 0 };
    uint16_t n = build(f, TYPE_DATA_SAMPLES, 0, s, 4);
    link_rx(f, n - 1);                              /* claim one byte short */
    CHECK(haptic_stats()->crc_err == 3, "LEN mismatch -> crc_err");
}

static void t_seq_gap_zero_fill(void)
{
    setup();
    int8_t a[4] = { 5, 6, 7, 8 };
    int8_t b[4] = { 9, 10, 11, 12 };
    rx(TYPE_DATA_SAMPLES, 0, a, 4);
    rx(TYPE_DATA_SAMPLES, 2, b, 4);                 /* gap = 1 frame */

    CHECK(haptic_stats()->seq_gap_frames == 1, "seq_gap_frames == 1");
    int8_t got[32];
    uint32_t n = mock_drain_fifo(got, 32);
    CHECK(n == 12, "4 + 4 zero-fill + 4 = 12, got %u", n);
    CHECK(got[4] == 0 && got[5] == 0 && got[6] == 0 && got[7] == 0,
          "gap filled with zeros (loss_policy 0)");
    CHECK(got[8] == 9, "post-gap samples follow the fill");
}

static void t_seq_gap_hold_last(void)
{
    setup();
    struct config_msg c;
    memset(&c, 0, sizeof c);
    c.version = PROTO_VERSION;
    c.loss_policy = 1;                              /* hold-last */
    rx(TYPE_CTRL_SET_CONFIG, 0, &c, sizeof c);

    int8_t a[3] = { 40, 41, 42 };
    int8_t b[3] = { 50, 51, 52 };
    rx(TYPE_DATA_SAMPLES, 1, a, 3);
    rx(TYPE_DATA_SAMPLES, 3, b, 3);                 /* gap = 1, fill 3 */

    int8_t got[32];
    uint32_t n = mock_drain_fifo(got, 32);
    CHECK(n == 9, "3 + 3 hold-last + 3, got %u", n);
    CHECK(got[3] == 42 && got[4] == 42 && got[5] == 42,
          "gap filled with the last sample value");
}

static void t_resync_big_gap(void)
{
    setup();
    int8_t a[4] = { 1, 2, 3, 4 };
    int8_t b[2] = { 7, 8 };
    rx(TYPE_DATA_SAMPLES, 0, a, 4);
    rx(TYPE_DATA_SAMPLES, 40, b, 2);               /* gap >= 16 -> resync */

    CHECK(haptic_stats()->resync == 1, "resync == 1");
    CHECK(g_mock.abort_calls == 1, "engine aborted on resync");
    int8_t got[16];
    uint32_t n = mock_drain_fifo(got, 16);
    CHECK(n == 2 && got[0] == 7, "FIFO flushed, only post-resync samples remain (n=%u)", n);
}

static void t_resync_frame(void)
{
    setup();
    int8_t a[4] = { 1, 2, 3, 4 };
    rx(TYPE_DATA_SAMPLES, 0, a, 4);
    rx(TYPE_RESYNC, 1, NULL, 0);

    CHECK(haptic_stats()->resync == 1, "explicit RESYNC counted");
    CHECK(g_mock.abort_calls == 1, "engine aborted");
    int8_t got[16];
    CHECK(mock_drain_fifo(got, 16) == 0, "FIFO flushed by RESYNC");

    /* SEQ reseated: a following in-order frame must not register a gap */
    int8_t b[2] = { 9, 9 };
    rx(TYPE_DATA_SAMPLES, 2, b, 2);
    CHECK(haptic_stats()->seq_gap_frames == 0, "no gap after RESYNC reseat");
}

static void t_ping_pong(void)
{
    setup();
    haptic_set_mode(HAPTIC_MODE_ENVELOPE);
    uint8_t nonce = 0xAB;
    rx(TYPE_CTRL_PING, 0, &nonce, 1);

    int i = cap_find(TYPE_CTRL_PONG);
    CHECK(i >= 0, "a PONG was sent");
    if (i >= 0) {
        struct pong_msg p;
        memcpy(&p, &cap_buf[i][PROTO_OFF_PAYLOAD], sizeof p);
        CHECK(cap_len[i] == sizeof(struct pong_msg) + PROTO_FRAME_OVERHEAD, "PONG frame size");
        CHECK(p.nonce == 0xAB, "nonce echoed");
        CHECK(p.proto_version == PROTO_VERSION, "PONG proto_version");
        CHECK(p.fw_major == FW_VERSION_MAJOR && p.fw_minor == FW_VERSION_MINOR, "PONG fw ver");
        CHECK(p.mode == MODE_ENVELOPE, "PONG reports current mode");
    }
}

static void t_status_rep(void)
{
    setup();
    g_mock.now_ms = 42000;
    int8_t s[3] = { 1, 2, 3 };
    rx(TYPE_DATA_SAMPLES, 0, s, 3);
    rx(TYPE_STATUS_REQ, 1, NULL, 0);

    int i = cap_find(TYPE_STATUS_REP);
    CHECK(i >= 0, "a STATUS_REP was sent");
    if (i >= 0) {
        CHECK(cap_len[i] == sizeof(struct stats_msg) + PROTO_FRAME_OVERHEAD, "STATUS frame size");
        struct stats_msg m;
        memcpy(&m, &cap_buf[i][PROTO_OFF_PAYLOAD], sizeof m);
        CHECK(m.proto_version == PROTO_VERSION, "STATUS proto_version");
        CHECK(m.uptime_s == 42, "uptime from haptic_now_ms()");
        CHECK(m.frames_rx == 2, "frames_rx counts DATA + STATUS_REQ");
        CHECK(m.fifo_fill == 3, "fifo_fill reflects buffered samples");
        CHECK(m.crc_err == 0 && m.bad_type == 0, "clean counters");
    }
}

static void t_set_config_ok(void)
{
    setup();
    struct config_msg c;
    memset(&c, 0, sizeof c);
    c.version        = PROTO_VERSION;
    c.sample_rate_hz = 2000;
    c.failsafe_ms    = 250;
    c.amp_max        = 100;
    c.loss_policy    = 1;
    c.ol_lra_period  = 70;
    rx(TYPE_CTRL_SET_CONFIG, 0, &c, sizeof c);

    CHECK(g_mock.apply_config_calls == 1, "config applied");
    CHECK(g_mock.last_cfg.sample_rate_hz == 2000, "sample_rate forwarded");
    CHECK(g_mock.last_cfg.failsafe_ms == 250, "failsafe forwarded");
    CHECK(g_mock.last_cfg.amp_max == 100, "amp_max forwarded");
    CHECK(g_mock.last_cfg.loss_policy == 1, "loss_policy forwarded");
    CHECK(g_mock.last_cfg.ol_lra_period == 70, "ol_lra_period forwarded");
    CHECK(cap_find(TYPE_STATUS_REP) >= 0, "STATUS_REP acknowledges the config");
    CHECK(cap_find(TYPE_FAULT) < 0, "no FAULT on a valid config");
    CHECK(link_data_blocked() == 0, "data plane open");
}

static void t_set_config_bad_field(void)
{
    setup();
    struct config_msg c;
    memset(&c, 0, sizeof c);
    c.version = PROTO_VERSION;
    c.sample_rate_hz = 100;                 /* < PROTO_RATE_MIN */
    rx(TYPE_CTRL_SET_CONFIG, 0, &c, sizeof c);

    CHECK(g_mock.apply_config_calls == 0, "nothing applied on a bad field");
    int i = cap_find(TYPE_FAULT);
    CHECK(i >= 0, "FAULT emitted");
    if (i >= 0) {
        struct fault_msg fm;
        memcpy(&fm, &cap_buf[i][PROTO_OFF_PAYLOAD], sizeof fm);
        CHECK(fm.code == FAULT_BAD_CONFIG, "FAULT_BAD_CONFIG");
    }

    /* wrong reserved byte */
    setup();
    memset(&c, 0, sizeof c);
    c.version = PROTO_VERSION;
    c.reserved = 1;
    rx(TYPE_CTRL_SET_CONFIG, 0, &c, sizeof c);
    CHECK(g_mock.apply_config_calls == 0, "nonzero reserved rejected");

    /* wrong payload length */
    setup();
    uint8_t junk[6] = { PROTO_VERSION, 0, 0, 0, 0, 0 };
    rx(TYPE_CTRL_SET_CONFIG, 0, junk, sizeof junk);
    CHECK(cap_find(TYPE_FAULT) >= 0, "short config payload -> FAULT");
    CHECK(g_mock.apply_config_calls == 0, "short config not applied");
}

static void t_version_mismatch(void)
{
    setup();
    struct config_msg c;
    memset(&c, 0, sizeof c);
    c.version = PROTO_VERSION + 1;
    rx(TYPE_CTRL_SET_CONFIG, 0, &c, sizeof c);

    int i = cap_find(TYPE_FAULT);
    CHECK(i >= 0, "FAULT on version mismatch");
    if (i >= 0) {
        struct fault_msg fm;
        memcpy(&fm, &cap_buf[i][PROTO_OFF_PAYLOAD], sizeof fm);
        CHECK(fm.code == FAULT_VERSION, "FAULT_VERSION");
        CHECK(fm.detail == PROTO_VERSION, "detail = box proto version");
    }
    CHECK(link_data_blocked() == 1, "data plane blocked");

    /* DATA is ignored while blocked */
    int8_t s[4] = { 1, 2, 3, 4 };
    rx(TYPE_DATA_SAMPLES, 1, s, 4);
    int8_t got[8];
    CHECK(mock_drain_fifo(got, 8) == 0, "DATA ignored while version-blocked");

    /* SET_MODE to non-idle is refused while blocked */
    cap_n = 0;
    uint8_t mode = MODE_SAMPLES;
    rx(TYPE_CTRL_SET_MODE, 2, &mode, 1);
    CHECK(g_mock.set_mode_calls == 0, "mode change refused while blocked");
    CHECK(cap_find(TYPE_FAULT) >= 0, "FAULT on blocked mode change");

    /* a good config clears the block */
    memset(&c, 0, sizeof c);
    c.version = PROTO_VERSION;
    rx(TYPE_CTRL_SET_CONFIG, 3, &c, sizeof c);
    CHECK(link_data_blocked() == 0, "good config re-opens the data plane");
    rx(TYPE_DATA_SAMPLES, 4, s, 4);
    CHECK(mock_drain_fifo(got, 8) == 4, "DATA flows again");
}

static void t_set_mode(void)
{
    setup();
    uint8_t mode = MODE_SAMPLES;
    rx(TYPE_CTRL_SET_MODE, 0, &mode, 1);
    CHECK(g_mock.set_mode_calls == 1, "valid mode applied");
    CHECK(g_mock.mode == HAPTIC_MODE_SAMPLES, "mode == SAMPLES");
    CHECK(cap_find(TYPE_STATUS_REP) >= 0, "STATUS_REP after mode change");

    setup();
    mode = 9;
    rx(TYPE_CTRL_SET_MODE, 0, &mode, 1);
    CHECK(g_mock.set_mode_calls == 0, "invalid mode rejected");
    int i = cap_find(TYPE_FAULT);
    CHECK(i >= 0, "FAULT on bad mode");
    if (i >= 0) {
        struct fault_msg fm;
        memcpy(&fm, &cap_buf[i][PROTO_OFF_PAYLOAD], sizeof fm);
        CHECK(fm.code == FAULT_BAD_MODE, "FAULT_BAD_MODE");
        CHECK(fm.detail == 9, "detail = offending mode byte");
    }
}

static void t_envelope(void)
{
    setup();
    struct env_cmd e = { .target = 200, .slew_ms = 15 };
    rx(TYPE_ENVELOPE, 0, &e, sizeof e);
    CHECK(g_mock.set_envelope_calls == 1, "envelope forwarded");
    CHECK(g_mock.env_target == 200 && g_mock.env_slew_ms == 15, "envelope args");
    CHECK(g_mock.notify_data_calls == 1, "envelope arms the failsafe");

    /* wrong length */
    setup();
    uint8_t one = 5;
    rx(TYPE_ENVELOPE, 0, &one, 1);
    CHECK(g_mock.set_envelope_calls == 0, "malformed envelope dropped");
    CHECK(haptic_stats()->crc_err == 1, "malformed envelope -> crc_err bucket");
}

static void t_unknown_type(void)
{
    setup();
    rx(0x55, 0, NULL, 0);
    CHECK(haptic_stats()->bad_type == 1, "bad_type counted");
    int i = cap_find(TYPE_LOG);
    CHECK(i >= 0, "a WARN LOG frame was emitted");
    if (i >= 0)
        CHECK(cap_buf[i][PROTO_OFF_PAYLOAD] == PROTO_LOG_WARN, "LOG level = WARN");
}

/* ==================================================================== */

int main(void)
{
    t_crc_vector();
    t_struct_sizes();
    t_data_samples_ok();
    t_bad_crc();
    t_short_and_oversize();
    t_seq_gap_zero_fill();
    t_seq_gap_hold_last();
    t_resync_big_gap();
    t_resync_frame();
    t_ping_pong();
    t_status_rep();
    t_set_config_ok();
    t_set_config_bad_field();
    t_version_mismatch();
    t_set_mode();
    t_envelope();
    t_unknown_type();

    printf("\n%s: %d checks, %d failures\n",
           g_fails ? "FAIL" : "PASS", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
