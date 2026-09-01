/******************************************************************************
 * haptic_engine.h — the renderer core (docs/ARCHITECTURE.md §2 layer 3).
 *
 * Owns the sample FIFO, the current output amplitude, the runtime config and
 * the stats counters. Consumes one sample per SysTick tick and drives the
 * DRV2605 (blocking I2C write, from the MAIN LOOP — not an ISR).
 *
 * Producer side (Phase 1: the local generator; Phase 2: the link layer):
 *   haptic_push_sample() / haptic_push_samples() / haptic_set_envelope()
 *   haptic_notify_data()   -- call on every valid data frame; arms the failsafe
 *
 * Consumer side (main loop):
 *   haptic_tick()          -- once per SysTick tick
 *   haptic_service()       -- once per loop iteration: failsafe + pending config
 *****************************************************************************/
#ifndef HAPTIC_ENGINE_H
#define HAPTIC_ENGINE_H

#include <stdint.h>
#include "haptic/haptic_fifo.h"

/* Values match docs/PROTOCOL.md §4.4 MODE_* (Phase 2 proto.h will reuse them). */
typedef enum {
    HAPTIC_MODE_IDLE       = 0,   /* output forced to 0 */
    HAPTIC_MODE_SAMPLES    = 1,   /* play the FIFO */
    HAPTIC_MODE_ENVELOPE   = 2,   /* ramp toward a target */
    HAPTIC_MODE_LOCAL_TEST = 3,   /* engine runs the built-in generator */
} haptic_mode_t;

/* Runtime config — seeded from config.h, later set by CTRL_SET_CONFIG. */
typedef struct {
    uint8_t  ol_lra_period;      /* DRV2605 reg 0x20; 0 = leave as configured */
    uint8_t  od_clamp;           /* DRV2605 reg 0x17; 0 = leave */
    uint16_t sample_rate_hz;     /* SysTick tick rate */
    uint16_t failsafe_ms;        /* no data for this long -> ramp to 0 */
    uint8_t  amp_max;            /* clamp on |sample|, 1..127 (0 treated as 127) */
    uint8_t  loss_policy;        /* 0 = zero-fill, 1 = hold-last (link layer) */
    uint8_t  underrun_decay_ms;  /* decay-to-0 time on FIFO underrun */
} haptic_config_t;

/* Counters. uint16/uint32, single-writer where it matters; torn reads OK for a
 * diagnostic. Serialised by the link layer on STATUS_REQ (Phase 2). */
typedef struct {
    uint32_t frames_rx;
    uint32_t samples_played;
    uint16_t fifo_overrun;
    uint16_t fifo_underrun;
    uint16_t i2c_err;
    uint16_t failsafe_trips;
    uint16_t tick_backlog_max;
    /* filled by the link layer (Phase 2): */
    uint16_t crc_err;
    uint16_t bad_type;
    uint16_t seq_gap_frames;
    uint16_t resync;
    /* filled by the main loop's ~10 Hz DRV2605 poll (Phase 1); surfaced in
     * STATUS_REP. Plain last-value snapshots, torn reads are fine. */
    uint8_t  drv_status;
    uint8_t  drv_vbat;
} haptic_stats_t;

void                   haptic_engine_init(void);   /* seed from config.h, mode IDLE, arm DRV2605 */
const haptic_config_t *haptic_config(void);
haptic_stats_t        *haptic_stats(void);
haptic_fifo_t         *haptic_fifo(void);          /* for direct fill / fill-level */

void          haptic_set_mode(haptic_mode_t m);    /* ramps through 0 before switching */
haptic_mode_t haptic_get_mode(void);

/* Milliseconds since the tick clock started. The engine is the renderer's
 * timekeeper of record; the link layer uses this for STATUS_REP uptime so it
 * needs no board/time dependency of its own. */
uint32_t      haptic_now_ms(void);

/* --- producer side --- */
int  haptic_push_sample(int8_t s);                 /* 1 ok, 0 FIFO full (overrun++) */
void haptic_push_samples(const int8_t *p, uint16_t n);
void haptic_set_envelope(uint8_t target, uint8_t slew_ms);
void haptic_notify_data(void);                     /* refresh the failsafe timer */

/* Abort the current playback safely: drain the FIFO and set the envelope
 * target to 0. The tick's decay ramp takes the output to 0 over
 * underrun_decay_ms. Used by the link layer for RESYNC and by the failsafe. */
void haptic_abort(void);

/* --- consumer side (main loop) --- */
void haptic_tick(void);       /* consume one tick, drive the DRV2605 */
void haptic_service(void);    /* failsafe check + apply a pending config change */

/* Apply a validated config; fields that are 0 keep the current value. May
 * reprogram OD_CLAMP / OL_LRA_PERIOD and change the tick rate. */
void haptic_apply_config(const haptic_config_t *c);

#endif /* HAPTIC_ENGINE_H */
