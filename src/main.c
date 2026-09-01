/********************************** (C) COPYRIGHT *******************************
 * File Name  : main.c
 * Description: OpenPulse firmware (CH570D) - entry point.
 *
 *  Two builds, selected by DRV2605_BENCH_TOOLS in config.h:
 *
 *  DRV2605_BENCH_TOOLS = 1  -> bench bring-up harness (Phase 0):
 *      board -> probe -> autocal -> resonance sweep -> sim-racing demo -> idle.
 *
 *  DRV2605_BENCH_TOOLS = 0  -> renderer (current default):
 *      board -> DRV2605 open-loop RTP armed -> haptic engine -> SysTick sample
 *      clock -> MODE_SAMPLES fed by the local sim-racing generator ~16 ms ahead
 *      of realtime. With HAPTIC_TEST_VIA_LINK=1 (Phase 2) the generator drives
 *      the real link layer: it frames DATA_SAMPLES and calls link_rx(), plus a
 *      boot PING/SET_CONFIG/SET_MODE handshake and a periodic STATUS_REQ. This
 *      is the exact path USB data will take in Phase 3. Prints '[R]' + '[TX ..]'
 *      lines. Set HAPTIC_TEST_VIA_LINK=0 for the direct-to-FIFO Phase 1 path.
 *
 *  Host log: 115200 8N1 - macOS `screen /dev/tty.usbmodem* 115200`.
 *  Docs: docs/ (architecture, protocol, hardware, roadmap, build).
 *******************************************************************************/

#include <string.h>
#include "CH57x_common.h"
#include "config.h"
#include "board/board.h"
#include "board/time.h"
#include "log/log.h"
#include "i2c/i2c_master.h"
#include "drv2605/drv2605.h"
#include "drv2605/drv2605_lra.h"
#include "drv2605/drv2605_bench.h"
#include "haptic/haptic_fifo.h"
#include "haptic/haptic_engine.h"
#include "haptic/haptic_patterns.h"
#include "link/link.h"
#include "link/proto.h"
#include "util/crc.h"

/* ========================================================================
 *  DRV2605_BENCH_TOOLS = 1  --  Phase 0 bench bring-up
 * ======================================================================== */
#if DRV2605_BENCH_TOOLS

__HIGH_CODE
int main(void)
{
    board_init();

    log_init();
    while (!log_is_connected())
    {
        mDelaymS(10);
    }

    mDelaymS(8000);

    log_puts("\r\n");
    log_puts("========================================\r\n");
    log_puts("  CH570D + DRV2605 haptic bring-up\r\n");
    log_puts("========================================\r\n");
    log_printf("  System clock : %lu Hz\r\n", (uint32_t)GetSysClock());
    log_printf("  I2C          : 400 kHz, SCL/PA8 SDA/PA9\r\n");
    log_printf("  DRV2605 addr : 0x%02X (7-bit 0x%02X)\r\n",
               DRV2605_ADDR8, DRV2605_ADDR7);
    log_printf("  Actuator     : %s\r\n",
               DRV2605_ACTUATOR_LRA ? "LRA" : "ERM");
    log_puts("========================================\r\n\r\n");

    log_puts("[STEP 1] I2C master init\r\n");
    i2c_master_init(DRV2605_I2C_HZ);

    log_puts("[STEP 2] assert DRV2605 EN, wait >250us\r\n");
    drv2605_en_high();
    mDelayuS(500);

    log_puts("[STEP 3] probe DRV2605 over I2C\r\n");
    if (drv2605_probe() != I2C_OK) {
        log_puts("\r\n*** DRV2605 not reachable - halting bring-up ***\r\n");
        goto idle;
    }

    log_puts("\r\n[STEP 4] reset + wake DRV2605\r\n");
    if (drv2605_reset_and_wake() != I2C_OK) {
        log_puts("\r\n*** DRV2605 init failed - halting bring-up ***\r\n");
        goto idle;
    }

    log_puts("\r\n[STEP 5] auto-calibration\r\n");
    if (drv2605_autocalibrate() != I2C_OK) {
        log_puts("\r\n*** Calibration failed - trying to play an effect "
                 "anyway (open-loop) ***\r\n");
    }

    log_puts("\r\n[STEP 6] play confirmation effects\r\n");
    drv2605_play_effect(DRV2605_TEST_EFFECT_1);
    mDelaymS(400);
    drv2605_play_effect(DRV2605_TEST_EFFECT_2);

    mDelaymS(400);
    drv2605_measure_resonance();

#if (DRV2605_ACTUATOR_LRA && !DRV2605_OL_PINNED)
    drv2605_lra_open_loop_sweep();
#endif
#if (DRV2605_ACTUATOR_LRA && DRV2605_OL_PINNED)
  #if DRV2605_RUN_MAX_TEST
    drv2605_max_drive_test();
  #endif
  #if DRV2605_RUN_SWEEP
    drv2605_lra_open_loop_sweep();
  #endif
    log_puts("\r\n[STEP 6] a few discrete impacts, then the sim-racing demo:\r\n");
    for (int i = 0; i < 3; i++) {
        haptic_pulse(DRV2605_CLICK_AMP, DRV2605_CLICK_MS, DRV2605_CLICK_BRAKE);
        mDelaymS(300);
    }
    haptic_demo_simracing(15);
#endif

    log_puts("\r\n=== Bring-up complete. Sim-racing demo loop, 15 s on / 8 s off. ===\r\n");

idle:
    {
        uint32_t beat = 0;
        while (1)
        {
            if (!log_is_connected())
            {
                while (!log_is_connected()) { mDelaymS(10); }
                log_puts("\r\n[RECONNECTED]\r\n");
            }

            log_printf("[cycle %4lu]\r\n", beat);

#if (DRV2605_ACTUATOR_LRA && DRV2605_OL_PINNED)
            haptic_demo_simracing(15);
            mDelaymS(8000);
#else
            {
                i2c_status_t st = drv2605_play_effect(DRV2605_TEST_EFFECT_1);
                if (st != I2C_OK)
                    log_printf("[idle] replay failed: %s\r\n", i2c_status_str(st));
                mDelaymS(3000);
            }
#endif
            beat++;
        }
    }
}

/* ========================================================================
 *  DRV2605_BENCH_TOOLS = 0  --  Phase 1 renderer
 * ======================================================================== */
#else

/* Re-print the boot header for this long after start, so a terminal attached
 * late still sees it. The CDC logger silently drops output while no host is
 * enumerated, so there is no point gating startup on the connection. */
#define BOOT_BANNER_MS   20000u

static uint8_t   g_probe_ok;
static uint8_t   g_devid;
static uint32_t  g_wr_us;         /* measured I2C RTP write time, us/byte */

#if HAPTIC_TEST_VIA_LINK
/* ---- box -> PC outbound sink. No transport yet (Phase 3 wires USB bulk IN);
 * for now decode the frame and log it, so the link path is observable. ---- */
static void box_tx(const uint8_t *f, uint16_t n)
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

static uint8_t g_rx_seq;         /* PC->box SEQ synthesised for the self-test */

/* Frame a message and hand it to link_rx(), exactly as a transport would. */
static void feed(uint8_t type, const void *payload, uint8_t len)
{
    uint8_t f[PROTO_MAX_FRAME];
    f[PROTO_OFF_TYPE] = type;
    f[PROTO_OFF_SEQ]  = g_rx_seq++;
    f[PROTO_OFF_LEN]  = len;
    if (len && payload) memcpy(&f[PROTO_OFF_PAYLOAD], payload, len);
    f[PROTO_OFF_PAYLOAD + len] = crc8_smbus(f, PROTO_OFF_PAYLOAD + len);
    link_rx(f, (uint16_t)(PROTO_OFF_PAYLOAD + len + 1));
}
#endif /* HAPTIC_TEST_VIA_LINK */

static void renderer_banner(void)
{
    log_puts("\r\n=== OpenPulse renderer (Phase 2) ===\r\n");
    log_printf("  DRV2605: %s  DEVICE_ID=%u (%s)  fw %u.%u  proto v%u\r\n",
               g_probe_ok ? "OK" : "NO ANSWER", g_devid,
               drv2605_devid_str(g_devid),
               FW_VERSION_MAJOR, FW_VERSION_MINOR, PROTO_VERSION);
    log_printf("  clock=%luHz  rate=%uHz  failsafe=%ums  fifo=%u\r\n",
               (uint32_t)GetSysClock(), haptic_config()->sample_rate_hz,
               haptic_config()->failsafe_ms, (unsigned)HAPTIC_FIFO_CAP);
    log_printf("  open-loop OL_LRA_PERIOD=%u (~%luHz)  I2C RTP write ~%luus/byte "
               "(max ~%luHz; rate=%u -> ~%lu%% bus if every sample changes)\r\n",
               haptic_config()->ol_lra_period,
               drv2605_olp_to_mhz(haptic_config()->ol_lra_period) / 1000u, g_wr_us,
               g_wr_us ? (1000000u / g_wr_us) : 0u,
               haptic_config()->sample_rate_hz,
               g_wr_us ? ((uint32_t)haptic_config()->sample_rate_hz * g_wr_us) / 10000u : 0u);
#if HAPTIC_TEST_VIA_LINK
    log_puts("  generator -> DATA_SAMPLES frames -> link_rx() (Phase 2 path).\r\n\r\n");
#else
    log_puts("  MODE_SAMPLES <- local sim-racing generator (direct FIFO).\r\n\r\n");
#endif
}

__HIGH_CODE
int main(void)
{
    board_init();
    log_init();
    mDelaymS(300);                        /* let USB enumerate if a host is there */

    /* ---- DRV2605 up ---- */
    i2c_master_init(DRV2605_I2C_HZ);
    drv2605_en_assert();
    mDelayuS(500);
    {
        uint8_t status = 0;
        g_probe_ok = (drv2605_read_reg(DRV2605_REG_STATUS, &status) == I2C_OK);
        g_devid = (uint8_t)((status >> 5) & 7);
    }
    drv2605_reset();
    drv2605_lra_configure();              /* arm open-loop RTP */

    /* ---- boot beacon: 3 taps — unmistakable "the renderer is running" ---- */
    for (int i = 0; i < 3; i++) {
        haptic_pulse(0x7F, 12, 1);
        mDelaymS(160);
    }

    haptic_engine_init();                 /* re-arms (MODE=RTP), output 0 */
    time_tick_start(haptic_config()->sample_rate_hz);

    /* ---- ticket 1.5: measure the real DRV2605 RTP write time ---- */
    {
        uint32_t t0 = time_ticks();
        for (int i = 0; i < 2000; i++)
            drv2605_write_reg(DRV2605_REG_RTP, (uint8_t)(i & 0x3F));
        uint32_t dt = time_ticks() - t0;              /* ms */
        drv2605_write_reg(DRV2605_REG_RTP, 0x00);
        g_wr_us = dt ? (dt * 1000u) / 2000u : 0u;
    }

    /* ---- enter SAMPLES mode ---- */
#if HAPTIC_TEST_VIA_LINK
    link_init(box_tx);
    {
        /* Self-test the control path: PING, a no-op SET_CONFIG (all fields 0 =
         * keep), then SET_MODE(SAMPLES). Replies are logged by box_tx(). */
        uint8_t nonce = 0x5A;
        struct config_msg cfg;
        uint8_t mode = MODE_SAMPLES;
        memset(&cfg, 0, sizeof cfg);
        cfg.version = PROTO_VERSION;
        feed(TYPE_CTRL_PING, &nonce, 1);
        feed(TYPE_CTRL_SET_CONFIG, &cfg, (uint8_t)sizeof cfg);
        feed(TYPE_CTRL_SET_MODE, &mode, 1);
    }
#else
    haptic_set_mode(HAPTIC_MODE_SAMPLES);
#endif
    while (haptic_get_mode() != HAPTIC_MODE_SAMPLES) haptic_tick();
    haptic_notify_data();

    const uint16_t rate = haptic_config()->sample_rate_hz;
    uint32_t lookahead = (uint32_t)HAPTIC_TEST_LOOKAHEAD_MS * rate / 1000u;
    if (lookahead < 2) lookahead = 2;

    uint32_t produced   = time_ticks();
    uint32_t serviced   = time_ticks();
    uint32_t report_ms  = 0;
    uint32_t banner_ms  = (uint32_t)-4000;    /* fire the first banner immediately */

    while (1)
    {
        uint32_t now = time_ticks();
        uint32_t ms  = time_now_ms();

        /* -- producer: keep the FIFO ~lookahead ahead of the tick clock -- */
        uint8_t feed_on = 1;
#if HAPTIC_TEST_STOP_PROD_S
        if (ms >= (uint32_t)HAPTIC_TEST_STOP_PROD_S * 1000u) feed_on = 0;
#endif
        if (feed_on) {
            uint32_t want = now + lookahead;
#if HAPTIC_TEST_VIA_LINK
            while (produced + HAPTIC_TEST_LINK_BATCH <= want &&
                   haptic_fifo_space(haptic_fifo()) >= HAPTIC_TEST_LINK_BATCH) {
                int8_t s[HAPTIC_TEST_LINK_BATCH];
                for (uint32_t k = 0; k < HAPTIC_TEST_LINK_BATCH; k++)
                    s[k] = haptic_pattern_simracing(produced + k, rate);
                feed(TYPE_DATA_SAMPLES, s, (uint8_t)HAPTIC_TEST_LINK_BATCH);
                produced += HAPTIC_TEST_LINK_BATCH;
            }
#else
            while (produced < want && haptic_fifo_space(haptic_fifo()) > 0) {
                if (!haptic_push_sample(haptic_pattern_simracing(produced, rate)))
                    break;
                produced++;
            }
            haptic_notify_data();
#endif
        }

        /* -- consumer: haptic_tick() once per elapsed SysTick -- */
        uint32_t pending = now - serviced;
        if (pending) {
            haptic_stats_t *st = haptic_stats();
            if (pending > st->tick_backlog_max)
                st->tick_backlog_max = (pending > 0xFFFFu) ? 0xFFFFu : (uint16_t)pending;
            if (pending > 8) pending = 8;              /* cap catch-up per loop */
            while (pending--) { haptic_tick(); serviced++; }
        }

        haptic_service();

        /* -- re-print the banner for the first BOOT_BANNER_MS -- */
        if (ms < BOOT_BANNER_MS && (ms - banner_ms) >= 4000u) {
            banner_ms = ms;
            renderer_banner();
        }

        /* -- '[R]' report: every 1 s early, then every 2 s -- */
        if ((ms - report_ms) >= ((ms < 20000u) ? 1000u : 2000u)) {
            report_ms = ms;
            uint8_t vb = 0, s = 0;
            drv2605_read_reg(DRV2605_REG_VBAT, &vb);
            drv2605_read_reg(DRV2605_REG_STATUS, &s);
            haptic_stats_t *st = haptic_stats();
            st->drv_vbat   = vb;
            st->drv_status = s;
            log_printf("[R] t=%3lus fifo=%2lu played=%lu ovr=%u unr=%u "
                       "backlog=%u i2c_err=%u fs=%u crc=%u gap=%u rsync=%u bad=%u "
                       "rx=%lu VBAT~%lumV STAT=0x%02X\r\n",
                       ms / 1000u, haptic_fifo_count(haptic_fifo()),
                       (unsigned long)st->samples_played, st->fifo_overrun,
                       st->fifo_underrun, st->tick_backlog_max, st->i2c_err,
                       st->failsafe_trips, st->crc_err, st->seq_gap_frames,
                       st->resync, st->bad_type, (unsigned long)st->frames_rx,
                       (uint32_t)vb * 5600u / 255u, s);
#if HAPTIC_TEST_VIA_LINK
            feed(TYPE_STATUS_REQ, NULL, 0);     /* exercise the STATUS_REP path */
#endif
        }
    }
}

#endif /* DRV2605_BENCH_TOOLS */
