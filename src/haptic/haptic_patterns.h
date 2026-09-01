/******************************************************************************
 * haptic_patterns.h — local, PC-independent haptic patterns.
 *
 * Boot self-test / bench / standalone-mode content. Not used once the PC is
 * streaming (docs/ARCHITECTURE.md §2, layer 3). LRA + open-loop only.
 *****************************************************************************/
#ifndef HAPTIC_PATTERNS_H
#define HAPTIC_PATTERNS_H

#include <stdint.h>

/**
 * One open-loop haptic pulse at the pinned resonance: drive at @p amp for
 * @p on_ms, then (if @p brake) a short reverse pulse (active brake), then idle.
 * Short bursts (10-20 ms) give a strong "tap" without heating the coil.
 * No-op if no open-loop frequency is pinned. No logging.
 */
void haptic_pulse(uint8_t amp, uint16_t on_ms, uint8_t brake);

/**
 * Demo of the intended sim-racing feel: continuously amplitude-modulated rumble
 * with a sharp "kerb" impact ~every 1.5 s, for @p seconds. Logs VBAT / chip
 * OVER_TEMP every ~2 s. Drives the DRV2605 directly (bench use).
 * In the real firmware the PC streams RTP instead.
 */
void haptic_demo_simracing(uint16_t seconds);

/**
 * Pure sim-racing waveform generator: the amplitude sample for tick index
 * @p sample_idx at @p sample_rate_hz. Same feel as haptic_demo_simracing()
 * but per-sample and side-effect-free — the engine's LOCAL_TEST mode and the
 * Phase 1 pipeline test feed the FIFO from this.
 * @return signed amplitude, 0..127 (never negative).
 */
int8_t haptic_pattern_simracing(uint32_t sample_idx, uint16_t sample_rate_hz);

#endif /* HAPTIC_PATTERNS_H */
