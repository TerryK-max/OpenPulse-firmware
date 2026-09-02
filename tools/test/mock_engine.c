/******************************************************************************
 * mock_engine.c — see mock_engine.h.
 *****************************************************************************/
#include <string.h>

#include "mock_engine.h"
#include "haptic/haptic_fifo.h"

mock_engine_t g_mock;

static haptic_fifo_t   s_fifo;
static haptic_config_t s_cfg;
static haptic_stats_t  s_stats;

/* Defaults mirror config.h so tests see realistic "keep" behaviour. */
static void seed_cfg(void)
{
    memset(&s_cfg, 0, sizeof s_cfg);
    s_cfg.ol_lra_period     = 64;
    s_cfg.od_clamp          = 0xC0;
    s_cfg.sample_rate_hz    = 1000;
    s_cfg.failsafe_ms       = 100;
    s_cfg.amp_max           = 127;
    s_cfg.loss_policy       = 0;
    s_cfg.underrun_decay_ms = 20;
}

void haptic_engine_init(void)
{
    memset(&g_mock, 0, sizeof g_mock);
    memset(&s_stats, 0, sizeof s_stats);
    haptic_fifo_reset(&s_fifo);
    seed_cfg();
    g_mock.mode = HAPTIC_MODE_IDLE;
}

void haptic_stats_reset(void) { memset(&s_stats, 0, sizeof s_stats); }

const haptic_config_t *haptic_config(void) { return &s_cfg; }
haptic_stats_t        *haptic_stats(void)  { return &s_stats; }
haptic_fifo_t         *haptic_fifo(void)   { return &s_fifo; }
haptic_mode_t          haptic_get_mode(void) { return g_mock.mode; }
uint32_t               haptic_now_ms(void)   { return g_mock.now_ms; }

void haptic_set_mode(haptic_mode_t m)
{
    g_mock.set_mode_calls++;
    g_mock.mode = m;
    haptic_fifo_reset(&s_fifo);       /* real engine drains on a mode change */
}

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
    g_mock.set_envelope_calls++;
    g_mock.env_target  = target;
    g_mock.env_slew_ms = slew_ms;
}

void haptic_notify_data(void) { g_mock.notify_data_calls++; }

void haptic_abort(void)
{
    g_mock.abort_calls++;
    haptic_fifo_reset(&s_fifo);
}

void haptic_tick(void)    { /* not exercised by the link tests */ }
void haptic_service(void) { /* mock applies config immediately */ }

void haptic_apply_config(const haptic_config_t *c)
{
    g_mock.apply_config_calls++;
    g_mock.last_cfg = *c;

    /* Same 0 = "keep" merge the real apply_cfg_now() does. */
    if (c->ol_lra_period)     s_cfg.ol_lra_period     = c->ol_lra_period;
    if (c->od_clamp)          s_cfg.od_clamp          = c->od_clamp;
    if (c->sample_rate_hz)    s_cfg.sample_rate_hz    = c->sample_rate_hz;
    if (c->failsafe_ms)       s_cfg.failsafe_ms       = c->failsafe_ms;
    if (c->amp_max)           s_cfg.amp_max           = c->amp_max;
    s_cfg.loss_policy = c->loss_policy;
    if (c->underrun_decay_ms) s_cfg.underrun_decay_ms = c->underrun_decay_ms;
}

uint32_t mock_drain_fifo(int8_t *buf, uint32_t cap)
{
    uint32_t n = 0;
    int8_t v;
    while (n < cap && haptic_fifo_pop(&s_fifo, &v)) buf[n++] = v;
    return n;
}
