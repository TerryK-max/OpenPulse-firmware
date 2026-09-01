/******************************************************************************
 * drv2605_lra.h — open-loop LRA drive primitives for the DRV2605L.
 *
 * The Apple Taptic Engine is driven open-loop at a fixed measured resonance
 * (docs/HARDWARE.md §3). These helpers program OL_LRA_PERIOD (reg 0x20) and
 * stream amplitude via RTP (reg 0x02). No logging.
 *****************************************************************************/
#ifndef DRV2605_LRA_H
#define DRV2605_LRA_H

#include <stdint.h>
#include "i2c/i2c_master.h"

/** Hz -> OL_LRA_PERIOD[6:0] value (reg 0x20). period_us = val x 98.46. Clamped 1..127. */
uint8_t  drv2605_hz_to_ol_period(uint16_t hz);

/** OL_LRA_PERIOD value -> frequency in milli-Hz (for logging). */
uint32_t drv2605_olp_to_mhz(uint8_t olp);

/** Effective OL_LRA_PERIOD from config (DRV2605_LRA_OPENLOOP_OLP / _HZ).
 *  0 => no open-loop frequency pinned (stay closed-loop). */
uint8_t  drv2605_effective_olp(void);

/** Set CONTROL3 LRA_OPEN_LOOP (bit0). If @p olp != 0 it is also written to
 *  OL_LRA_PERIOD (reg 0x20). No-op on an ERM build. */
void drv2605_lra_set_open_loop(uint8_t on, uint8_t olp);

/** Program the actuator for open-loop RTP drive at the configured resonance:
 *  OD_CLAMP, N_ERM_LRA/ERM_OPEN_LOOP, LRA_OPEN_LOOP + OL_LRA_PERIOD, MODE=RTP.
 *  After this call the actuator is armed and drv2605_set_amplitude() drives it. */
void drv2605_lra_configure(void);

/** Write an amplitude sample to RTP (reg 0x02). Signed: 0 = silent, +127 =
 *  OD_CLAMP full scale, negatives drive the opposite phase. Skips the I2C write
 *  when the value is unchanged from the last successful write. */
i2c_status_t drv2605_set_amplitude(int8_t v);

/* Continuous RTP amplitude streaming (bench / patterns).
 *   drv2605_rtp_open()   MODE=RTP, open-loop @ effective_olp, OD_CLAMP set, cache reset.
 *   drv2605_rtp_level(v) same as drv2605_set_amplitude((int8_t)v).
 *   drv2605_rtp_close()  output 0, back to internal-trigger idle.
 * THERMAL LIMITS: docs/HARDWARE.md §4. */
void drv2605_rtp_open(void);
void drv2605_rtp_level(uint8_t lvl);
void drv2605_rtp_close(void);

#endif /* DRV2605_LRA_H */
