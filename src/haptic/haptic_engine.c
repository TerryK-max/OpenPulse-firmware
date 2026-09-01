#include <string.h>
#include "CH57x_common.h"
#include "config.h"
#include "board/time.h"
#include "drv2605/drv2605.h"
#include "drv2605/drv2605_lra.h"
#include "haptic/haptic_engine.h"
#include "haptic/haptic_patterns.h"

static haptic_fifo_t   s_fifo;
static haptic_config_t s_cfg;
static haptic_stats_t  s_stats;

static haptic_mode_t s_mode         = HAPTIC_MODE_IDLE;
static haptic_mode_t s_pending_mode = HAPTIC_MODE_IDLE;

static int8_t   s_last_out;              /* last amplitude written */
static int      s_env_current;           /* ENVELOPE current level (amplitude units) */
static int      s_env_target;
static int      s_env_step = 1;
static int      s_decay_step = 1;        /* per-tick ramp toward 0 (underrun / mode switch) */
static uint32_t s_local_idx;             /* LOCAL_TEST generator index */

static uint32_t s_last_data_ms;
static uint8_t  s_failsafe_active;

static haptic_config_t s_pending_cfg;
static uint8_t          s_have_pending_cfg;

/* -------------------------------------------------------------------------- */

static void recompute_derived(void)
{
    uint32_t dm   = s_cfg.underrun_decay_ms ? s_cfg.underrun_decay_ms : 20u;
    uint32_t rate = s_cfg.sample_rate_hz ? s_cfg.sample_rate_hz : 1000u;
    uint32_t den  = dm * rate;
    s_decay_step  = den ? (int)((127u * 1000u + den - 1u) / den) : 127;
    if (s_decay_step < 1) s_decay_step = 1;
}

static int8_t step_toward_zero(int8_t v, int step)
{
    if (v >  step) return (int8_t)(v - step);
    if (v < -step) return (int8_t)(v + step);
    return 0;
}

static void apply_cfg_now(const haptic_config_t *c)
{
    haptic_config_t n = s_cfg;

    if (c->ol_lra_period)      n.ol_lra_period      = c->ol_lra_period;
    if (c->od_clamp)           n.od_clamp           = c->od_clamp;
    if (c->sample_rate_hz)     n.sample_rate_hz     = c->sample_rate_hz;
    if (c->failsafe_ms)        n.failsafe_ms        = c->failsafe_ms;
    if (c->amp_max)            n.amp_max            = c->amp_max;
    n.loss_policy = c->loss_policy;
    if (c->underrun_decay_ms)  n.underrun_decay_ms  = c->underrun_decay_ms;

    uint8_t drv  = (n.ol_lra_period != s_cfg.ol_lra_period) ||
                   (n.od_clamp      != s_cfg.od_clamp);
    uint8_t tick = (n.sample_rate_hz != s_cfg.sample_rate_hz);

    s_cfg = n;
    recompute_derived();

    if (drv) {
        drv2605_write_reg(DRV2605_REG_CLAMPV, s_cfg.od_clamp);
        drv2605_lra_set_open_loop(1, s_cfg.ol_lra_period);
    }
    if (tick) time_tick_start(s_cfg.sample_rate_hz);
}

/* -------------------------------------------------------------------------- */

void haptic_engine_init(void)
{
    memset(&s_stats, 0, sizeof s_stats);

    s_cfg.ol_lra_period     = drv2605_effective_olp();
    s_cfg.od_clamp          = DRV2605_OD_CLAMP;
    s_cfg.sample_rate_hz    = HAPTIC_SAMPLE_RATE_HZ;
    s_cfg.failsafe_ms       = HAPTIC_FAILSAFE_MS;
    s_cfg.amp_max           = HAPTIC_AMP_MAX;
    s_cfg.loss_policy       = HAPTIC_LOSS_POLICY;
    s_cfg.underrun_decay_ms = HAPTIC_UNDERRUN_DECAY_MS;

    haptic_fifo_reset(&s_fifo);
    s_mode = s_pending_mode = HAPTIC_MODE_IDLE;
    s_last_out = 0;
    s_env_current = s_env_target = 0;
    s_env_step = 1;
    s_local_idx = 0;
    s_last_data_ms = 0;
    s_failsafe_active = 0;
    s_have_pending_cfg = 0;

    recompute_derived();

    drv2605_lra_configure();          /* arm: open-loop RTP at the pinned resonance */
    (void)drv2605_set_amplitude(0);
}

const haptic_config_t *haptic_config(void) { return &s_cfg; }
haptic_stats_t        *haptic_stats(void)  { return &s_stats; }
haptic_fifo_t         *haptic_fifo(void)   { return &s_fifo; }
haptic_mode_t          haptic_get_mode(void) { return s_mode; }
uint32_t               haptic_now_ms(void)   { return time_now_ms(); }

void haptic_set_mode(haptic_mode_t m)
{
    /* Discard whatever was queued for the OLD mode now, at request time — not
     * when the ramp-through-0 completes. Samples the producer pushes *after*
     * this call are for the new mode (e.g. a PC priming the FIFO right behind
     * its SET_MODE(SAMPLES)) and must survive the transition. */
    if (m != s_mode || m != s_pending_mode)
        haptic_fifo_reset(&s_fifo);
    s_pending_mode = m;
    haptic_notify_data();
}

/* --- producer side --- */

int haptic_push_sample(int8_t s)
{
    if (haptic_fifo_push(&s_fifo, s)) return 1;
    if (s_stats.fifo_overrun < 0xFFFF) s_stats.fifo_overrun++;
    return 0;
}

void haptic_push_samples(const int8_t *p, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) (void)haptic_push_sample(p[i]);
}

void haptic_set_envelope(uint8_t target, uint8_t slew_ms)
{
    int tgt = (int)target * 127 / 255;
    s_env_target = tgt;
    if (slew_ms == 0) {
        s_env_step = 128;
    } else {
        uint32_t ticks = (uint32_t)slew_ms * s_cfg.sample_rate_hz / 1000u;
        int dist = tgt - s_env_current;
        if (dist < 0) dist = -dist;
        s_env_step = ticks ? (int)(((uint32_t)dist + ticks - 1u) / ticks) : 128;
        if (s_env_step < 1) s_env_step = 1;
    }
    haptic_notify_data();
}

void haptic_notify_data(void)
{
    s_last_data_ms = time_now_ms();
    s_failsafe_active = 0;
}

void haptic_abort(void)
{
    haptic_fifo_reset(&s_fifo);
    s_env_target = 0;
}

/* --- consumer side --- */

void haptic_tick(void)
{
    int8_t out;

    if (s_mode != s_pending_mode) {
        out = step_toward_zero(s_last_out, s_decay_step);
        if (out == 0) {
            s_mode = s_pending_mode;
            /* FIFO was already flushed in haptic_set_mode(); anything here now
             * was pushed for this (new) mode. */
            s_env_current = 0;
            s_local_idx = 0;
        }
    } else {
        switch (s_mode) {
        case HAPTIC_MODE_SAMPLES: {
            int8_t s;
            if (!s_failsafe_active && haptic_fifo_pop(&s_fifo, &s)) {
                out = s;
                s_stats.samples_played++;
            } else {
                if (!s_failsafe_active && s_stats.fifo_underrun < 0xFFFF)
                    s_stats.fifo_underrun++;
                out = step_toward_zero(s_last_out, s_decay_step);
            }
            break;
        }
        case HAPTIC_MODE_ENVELOPE:
            if (s_env_current < s_env_target) {
                s_env_current += s_env_step;
                if (s_env_current > s_env_target) s_env_current = s_env_target;
            } else if (s_env_current > s_env_target) {
                s_env_current -= s_env_step;
                if (s_env_current < s_env_target) s_env_current = s_env_target;
            }
            out = (int8_t)s_env_current;
            break;
        case HAPTIC_MODE_LOCAL_TEST:
            out = haptic_pattern_simracing(s_local_idx++, s_cfg.sample_rate_hz);
            break;
        case HAPTIC_MODE_IDLE:
        default:
            out = 0;
            break;
        }
    }

    {
        int lim = s_cfg.amp_max ? s_cfg.amp_max : 127;
        if (out >  lim) out = (int8_t)lim;
        if (out < -lim) out = (int8_t)(-lim);
    }

    if (drv2605_set_amplitude(out) != I2C_OK) {
        if (s_stats.i2c_err < 0xFFFF) s_stats.i2c_err++;
    }
    s_last_out = out;
}

void haptic_service(void)
{
    if (s_have_pending_cfg) {
        s_have_pending_cfg = 0;
        apply_cfg_now(&s_pending_cfg);
    }

    if (s_mode == HAPTIC_MODE_SAMPLES || s_mode == HAPTIC_MODE_ENVELOPE) {
        uint32_t age = time_now_ms() - s_last_data_ms;
        if (s_cfg.failsafe_ms && age > s_cfg.failsafe_ms) {
            haptic_abort();
            if (!s_failsafe_active) {
                s_failsafe_active = 1;
                if (s_stats.failsafe_trips < 0xFFFF) s_stats.failsafe_trips++;
            }
        }
    }
}

void haptic_apply_config(const haptic_config_t *c)
{
    s_pending_cfg = *c;
    s_have_pending_cfg = 1;
}
