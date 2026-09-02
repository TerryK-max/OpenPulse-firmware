/******************************************************************************
 * bench_trace.h — GPIO instrumentation for scope / logic-analyzer latency and
 * jitter measurement (docs/BENCH.md). Header-only, compiled out entirely when
 * BENCH_GPIO_TRACE == 0 (the default).
 *
 *   PA4  (BENCH_TRACE_RX_PIN)    toggles on each DATA_SAMPLES frame handed to
 *                                the link layer  -> "samples arrived" instant
 *   PA10 (BENCH_TRACE_DRIVE_PIN) toggles on each drv2605_set_amplitude() call
 *                                -> "actuator command updated" instant (= the
 *                                render tick)
 *
 * On a scope: PA10's period = render jitter; a PA4 edge to the next relevant
 * PA10 edge = rx -> playout latency (shrinks as the PC lookahead shrinks — run
 * `tools/bench/bench.py --latency-probe`); a third probe on the DRV2605 output
 * adds the driver + LRA mechanical rise.
 *
 * PA4 and PA10 are otherwise free (docs/HARDWARE.md §1.1). Enabling this claims
 * them: nothing else may use them while BENCH_GPIO_TRACE is set.
 *****************************************************************************/
#ifndef BENCH_TRACE_H
#define BENCH_TRACE_H

#include "config.h"

#if BENCH_GPIO_TRACE

#include "CH57x_common.h"

#define BENCH_TRACE_RX_PIN     GPIO_Pin_4
#define BENCH_TRACE_DRIVE_PIN  GPIO_Pin_10

static inline void bench_trace_init(void)
{
    GPIOA_ModeCfg(BENCH_TRACE_RX_PIN | BENCH_TRACE_DRIVE_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_ResetBits(BENCH_TRACE_RX_PIN | BENCH_TRACE_DRIVE_PIN);
}
static inline void bench_trace_rx(void)    { GPIOA_InverseBits(BENCH_TRACE_RX_PIN); }
static inline void bench_trace_drive(void) { GPIOA_InverseBits(BENCH_TRACE_DRIVE_PIN); }

#else  /* BENCH_GPIO_TRACE == 0 : no dependency on the SDK at all */

static inline void bench_trace_init(void)  { }
static inline void bench_trace_rx(void)    { }
static inline void bench_trace_drive(void) { }

#endif

#endif /* BENCH_TRACE_H */
