/******************************************************************************
 * drv2605_bench.h — verbose DRV2605L bring-up / diagnostic harness.
 *
 * This is scaffolding: probe, auto-calibration attempts, the open-loop
 * resonance sweep, and the sim-racing demo, all with heavy USB logging. It
 * exists so the actuator can be characterised on the bench. Phase 1 replaces
 * the boot flow with the real renderer and sets DRV2605_BENCH_TOOLS = 0.
 *
 * Compiled only when DRV2605_BENCH_TOOLS (config.h). Depends on log/.
 *****************************************************************************/
#ifndef DRV2605_BENCH_H
#define DRV2605_BENCH_H

#include "config.h"

#if DRV2605_BENCH_TOOLS

#include "i2c/i2c_master.h"

/** Drive the DRV2605 EN pin high (or note it is hard-wired). Logs the pin. */
void drv2605_en_high(void);

/** Read STATUS up to 3x (recovering the bus on BUS_BUSY), decode DEVICE_ID. */
i2c_status_t drv2605_probe(void);

/** DEV_RESET, wait for self-clear, leave STANDBY (MODE = 0). */
i2c_status_t drv2605_reset_and_wake(void);

/** Full auto-calibration attempt sequence. Non-fatal on failure (see
 *  docs/HARDWARE.md §3 — it does not converge with the Apple actuator). */
i2c_status_t drv2605_autocalibrate(void);

/** Fire one ROM library effect and wait for GO to self-clear. */
i2c_status_t drv2605_play_effect(uint8_t effect_id);

/** Drive constant RTP and sample LRA_PERIOD (0x22); print the (unreliable)
 *  closed-loop resonance reading with /2 and /3 subharmonic interpretations. */
void drv2605_measure_resonance(void);

/** Sweep open-loop drive across DRV2605_SWEEP_HZ_* — the reliable way to find
 *  the mechanical resonance by feel. Logs each step + VBAT. */
void drv2605_lra_open_loop_sweep(void);

#if (DRV2605_ACTUATOR_LRA && DRV2605_RUN_MAX_TEST)
/** 2 s continuous full-power open-loop drive with VBAT/STATUS logging. Heats
 *  the actuator — opt-in via DRV2605_RUN_MAX_TEST. */
void drv2605_max_drive_test(void);
#endif

#endif /* DRV2605_BENCH_TOOLS */
#endif /* DRV2605_BENCH_H */
