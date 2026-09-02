/******************************************************************************
 * config.h — all compile-time tunables for the OpenPulse firmware (CH570D).
 *
 * Phase 0 refactor: these #defines were moved verbatim out of the old
 * src/Main.c. Behaviour is unchanged. See docs/ROADMAP.md Phase 0.
 *
 * Values tagged  [RUNTIME]  will become fields of a `struct config` (seeded
 * from here) once the protocol's CTRL_SET_CONFIG lands — Phase 1.3 / Phase 2.
 * Values tagged  [MEASURED] were found on the bench — do NOT change without a
 * new on-hardware test (see docs/HARDWARE.md).
 *****************************************************************************/
#ifndef CONFIG_H
#define CONFIG_H

#include "drv2605/drv2605_regs.h"

/* ======================================================================
 *  Firmware identity (reported in CTRL_PONG / STATUS_REP — docs/PROTOCOL.md)
 * ====================================================================== */
#define FW_VERSION_MAJOR  0
#define FW_VERSION_MINOR  2      /* Phase 2: link layer + protocol */

/* ======================================================================
 *  Board wiring
 * ====================================================================== */

/* DRV2605L EN pin. It must be high before the chip accepts I2C writes.
 *   - EN on a CH570D GPIO  -> set DRV2605_EN_PIN (PAn).
 *   - EN hard-wired to VDD -> define DRV2605_EN_HARDWIRED.
 * PA8/PA9 = I2C, PA0/PA1 = USB, PA7 = reserved debug UART — do not reuse. */
//#define DRV2605_EN_HARDWIRED
#define DRV2605_EN_PIN          GPIO_Pin_11     /* [MEASURED] PA11 -> DRV2605.EN */

#define DRV2605_I2C_HZ          400000          /* DRV2605L maximum */

/* ======================================================================
 *  Actuator
 * ====================================================================== */

/* 0 = ERM, 1 = LRA. Drives N_ERM_LRA (reg 0x1A bit7) and the ROM library. */
#define DRV2605_ACTUATOR_LRA   1

#if DRV2605_ACTUATOR_LRA
/* ---- LRA parameters -- Apple Taptic Engine (iPhone XS Max), DRV2605L -----
 * Closed-loop / auto-cal do NOT work with this actuator (weak back-EMF); we
 * drive open-loop at a measured fixed resonance. Full rationale + evidence in
 * docs/HARDWARE.md §3.                                                      */
  #define DRV2605_LIBRARY        6      /* LRA ROM library (bench effects only) */
  #define DRV2605_RATED_VOLTAGE  0x50   /* reg 0x16 - closed-loop only, ignored open-loop */
/* OD_CLAMP (reg 0x17) = open-loop full-scale / PEAK voltage reference.
 *   0x50~=1.7V  0x80~=2.7V  0xA0~=3.4V  0xC0~=4.1V  0xE0~=4.75V
 * Output = OD_CLAMP x (|RTP|/127); sustained loudness is set by the RTP level
 * (DRV2605_RUMBLE_AMP), OD_CLAMP only has to cover the loudest impact.       */
  #define DRV2605_OD_CLAMP       0xC0   /* [RUNTIME][MEASURED] ~4.1 V peak ceiling */
  #define DRV2605_DRIVE_TIME     0x1B   /* reg 0x1B[4:0] - closed-loop guess only */
  #define DRV2605_FB_BRAKE_FAC   2      /* reg 0x1A[6:4] - cal only */
  #define DRV2605_LOOP_GAIN      1      /* reg 0x1A[3:2] - cal only */
  #define DRV2605_BEMF_GAIN      3      /* reg 0x1A[1:0] - cal only */
  #define DRV2605_AUTO_CAL_TIME  3      /* reg 0x1E[5:4] - cal only */

/* Open-loop frequency (DRV2605L reg 0x20 OL_LRA_PERIOD), checked in this order:
 *   DRV2605_LRA_OPENLOOP_OLP : raw reg-0x20 value from the sweep log.
 *   DRV2605_LRA_OPENLOOP_HZ  : Hz (firmware converts).   Both 0 => closed-loop.
 * [MEASURED] 64 -> 6301 us -> ~158.7 Hz.                                     */
  #define DRV2605_LRA_OPENLOOP_OLP  64   /* [RUNTIME][MEASURED] */
  #define DRV2605_LRA_OPENLOOP_HZ   0

/* Sim-racing feel (drv2605_demo_simracing). RUMBLE_AMP is the sustained RTP
 * level and the main thermal driver — keep it modest. */
  #define DRV2605_RUMBLE_AMP     0x48   /* [RUNTIME] sustained RTP level */
  #define DRV2605_IMPACT_AMP     0x7F   /* transient hit level */
  #define DRV2605_RTP_LEVEL      0x7F   /* level for bench sweep / max-drive test */

/* Discrete tap (drv2605_lra_pulse). */
  #define DRV2605_CLICK_AMP      0x7F
  #define DRV2605_CLICK_MS       14
  #define DRV2605_CLICK_BRAKE    1

/* Bench sweep window (fine, around the felt resonance). Widen to re-locate. */
  #define DRV2605_SWEEP_HZ_HI    168
  #define DRV2605_SWEEP_HZ_LO    148
  #define DRV2605_SWEEP_HZ_STEP  1
#else
/* ---- ERM parameters (register power-on defaults) ---------------------- */
  #define DRV2605_LIBRARY        1
  #define DRV2605_RATED_VOLTAGE  0x3F
  #define DRV2605_OD_CLAMP       0x89
  #define DRV2605_DRIVE_TIME     0x13
  #define DRV2605_FB_BRAKE_FAC   2
  #define DRV2605_LOOP_GAIN      2
  #define DRV2605_BEMF_GAIN      2
  #define DRV2605_AUTO_CAL_TIME  3
#endif

/* Auto-cal attempts before giving up (failed cal is non-fatal). */
#define DRV2605_CAL_ATTEMPTS   2

/* Bench ROM effect IDs (see EVT/DRV2605_interface.md effect list). */
#define DRV2605_TEST_EFFECT_1  1        /* "Strong Click - 100%" */
#define DRV2605_TEST_EFFECT_2  16       /* "1000 ms Alert 100%"  */

/* Is an open-loop frequency pinned? (config-derived) */
#if DRV2605_ACTUATOR_LRA && (DRV2605_LRA_OPENLOOP_OLP || DRV2605_LRA_OPENLOOP_HZ)
  #define DRV2605_OL_PINNED  1
#else
  #define DRV2605_OL_PINNED  0
#endif

/* FEEDBACK_CONTROL (0x1A) and CONTROL4 (0x1E) values composed from the above.
 * Used only by the calibration path. */
#define DRV2605_FEEDBACK_CFG       ( DRV2605_N_ERM_LRA(DRV2605_ACTUATOR_LRA)   \
                                   | (((DRV2605_FB_BRAKE_FAC) & 7u) << 4)      \
                                   | (((DRV2605_LOOP_GAIN)    & 3u) << 2)      \
                                   | (((DRV2605_BEMF_GAIN)    & 3u) << 0) )
#define DRV2605_AUTO_CAL_TIME_BITS (((DRV2605_AUTO_CAL_TIME) & 3u) << 4)

/* ======================================================================
 *  Haptic engine (Phase 1) — docs/ARCHITECTURE.md §3, docs/PROTOCOL.md §4.3
 *  All [RUNTIME]: seeds for `struct haptic_config`, overridable by
 *  CTRL_SET_CONFIG once the link layer lands (Phase 2).
 * ====================================================================== */
#define HAPTIC_SAMPLE_RATE_HZ    1000    /* [RUNTIME] SysTick tick / sample rate (250..4000) */
#define HAPTIC_FAILSAFE_MS       100     /* [RUNTIME] no data this long -> ramp to 0 */
#define HAPTIC_AMP_MAX           127     /* [RUNTIME] clamp on |sample| (1..127; 127 = none) */
#define HAPTIC_LOSS_POLICY       0       /* [RUNTIME] 0 = zero-fill, 1 = hold-last (link layer) */
#define HAPTIC_UNDERRUN_DECAY_MS 20      /* [RUNTIME] ramp-to-0 time on FIFO underrun */

/* Sample FIFO depth (power of two). ~128 ms at 1 kHz. Bigger = more loss
 * tolerance, more latency. */
#define HAPTIC_FIFO_CAP          128u

/* Phase 1 pipeline test: the main loop feeds the FIFO from the built-in
 * sim-racing generator, ~16 ms ahead of realtime.
 *   HAPTIC_TEST_LOOKAHEAD_MS  producer lead over the tick clock.
 *   HAPTIC_TEST_STOP_PROD_S   if != 0, stop feeding after this many seconds so
 *                             the failsafe can be observed tripping. */
#define HAPTIC_TEST_LOOKAHEAD_MS 16
#define HAPTIC_TEST_STOP_PROD_S  0

/* Phase 2: route the generator through the real link layer instead of pushing
 * straight to the FIFO. The renderer builds DATA_SAMPLES frames of this many
 * samples and feeds them to link_rx() — same path USB data will take in
 * Phase 3. 0 = keep the direct-to-FIFO Phase 1 path. */
#define HAPTIC_TEST_VIA_LINK     1
#define HAPTIC_TEST_LINK_BATCH   8       /* samples per DATA_SAMPLES frame */

/* ======================================================================
 *  USB (Phase 3) — composite device: CDC-ACM (logs) + vendor bulk (protocol)
 *  docs/HARDWARE.md §5, docs/ARCHITECTURE.md §2 layer 5.
 * ====================================================================== */
#define USB_VID                0x1A86u   /* Nanjing Qinheng (WCH) */
#define USB_PID                0x5730u   /* OpenPulse firmware (composite) */

/* CDC log TX ring (EP1 IN). Power of two. Bytes dropped when full — logs are
 * best-effort, never block the hot path. */
#define USB_LOG_RING_SIZE      512u

/* Vendor protocol frame staging (EP2). Each slot holds one 64-byte USB packet
 * = one protocol frame (<= 60 B, docs/PROTOCOL.md §1). Power of two. RX is
 * filled by the USB ISR and drained by transport_usb.poll() every main-loop
 * iteration (sub-ms); TX the reverse, the ISR draining at ~1 frame/ms. */
#define USB_VENDOR_RX_DEPTH    8u        /* inbound frames buffered from the host */
#define USB_VENDOR_TX_DEPTH    4u        /* outbound frames queued to the host */

/* ======================================================================
 *  Feature flags
 * ====================================================================== */

/* 1 = build + run the bench bring-up harness (probe/cal/sweep/demo, verbose).
 * 0 = build the Phase 1 renderer (haptic engine + FIFO + tick + failsafe,
 *     fed by the local generator). The bench harness stays in the tree either
 *     way — it is one #define away. */
#define DRV2605_BENCH_TOOLS    0

/* Bench: 2 s continuous full-power drive with supply diagnostics. Heats the
 * actuator — opt-in only. */
#define DRV2605_RUN_MAX_TEST   0

/* Bench: run the fine open-loop sweep at boot (STEP 6). */
#define DRV2605_RUN_SWEEP      1

/* Phase 3: log a one-line decode of every frame the box sends on the vendor
 * bulk IN endpoint, to the CDC console. Handy for the first bench bring-up
 * (confirms enumeration + EP2 IN before the PC sender exists); turn off once a
 * PC dashboard is polling STATUS at ~10 Hz or it floods the log. */
#define TRANSPORT_USB_TX_TRACE  1

/* Phase 3.5 bench: toggle PA4 on each DATA frame and PA10 on each actuator
 * update, for scope / logic-analyzer latency + jitter measurement. Claims PA4
 * and PA10. See src/bench/bench_trace.h and docs/BENCH.md. 0 = compiled out. */
#define BENCH_GPIO_TRACE  0

#endif /* CONFIG_H */
