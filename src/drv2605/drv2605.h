/******************************************************************************
 * drv2605.h — generic DRV2605L register access.
 *
 * Thin wrappers over i2c_master with the DRV2605 address baked in, plus a few
 * chip helpers. No logging, no policy. Open-loop LRA specifics live in
 * drv2605_lra.h; the verbose bring-up harness lives in drv2605_bench.h.
 *****************************************************************************/
#ifndef DRV2605_H
#define DRV2605_H

#include <stdint.h>
#include "drv2605/drv2605_regs.h"
#include "i2c/i2c_master.h"

static inline i2c_status_t drv2605_write_reg(uint8_t reg, uint8_t val)
{
    return i2c_write_reg(DRV2605_ADDR8, reg, val);
}

static inline i2c_status_t drv2605_write(uint8_t reg, const uint8_t *data, uint8_t len)
{
    return i2c_write(DRV2605_ADDR8, reg, data, len);
}

static inline i2c_status_t drv2605_read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_read_reg(DRV2605_ADDR8, reg, val);
}

/** Poll the self-clearing GO bit. @return I2C_OK when clear, I2C_ERR_TIMEOUT on
 *  expiry, or an I2C error. Uses ~10 ms polling like the bring-up. */
i2c_status_t drv2605_wait_go_clear(uint16_t timeout_ms);

/** DEVICE_ID[2:0] -> human string (3=DRV2605, 6=DRV2604L, 7=DRV2605L, ...). */
const char *drv2605_devid_str(uint8_t id);

/** Drive the EN pin high (config: DRV2605_EN_PIN / DRV2605_EN_HARDWIRED).
 *  No-op when EN is hard-wired. No logging. */
void drv2605_en_assert(void);

/** DEV_RESET, wait for it to self-clear (~40 ms budget), leave STANDBY
 *  (MODE = internal trigger). No logging. */
i2c_status_t drv2605_reset(void);

#endif /* DRV2605_H */
