#include "CH57x_common.h"
#include "config.h"
#include "drv2605/drv2605.h"
#include "drv2605/drv2605_lra.h"

/* Shadow of RTP (reg 0x02) so drv2605_set_amplitude() can skip redundant writes.
 * Reset by drv2605_rtp_open() / drv2605_lra_configure(). */
static uint8_t s_rtp_cache;
static uint8_t s_rtp_primed;

uint8_t drv2605_hz_to_ol_period(uint16_t hz)
{
    uint32_t per_us = (hz ? (1000000u / hz) : 6000u);
    uint32_t v = (per_us * 100u + 4923u) / 9846u;      /* rounded */
    if (v < 1u)   v = 1u;
    if (v > 127u) v = 127u;
    return (uint8_t)v;
}

uint32_t drv2605_olp_to_mhz(uint8_t olp)              /* -> milli-Hz */
{
    uint32_t per_us = (uint32_t)olp * 9846u / 100u;
    return per_us ? (1000000000u / per_us) : 0u;
}

uint8_t drv2605_effective_olp(void)
{
#if DRV2605_ACTUATOR_LRA && DRV2605_LRA_OPENLOOP_OLP
    return (uint8_t)DRV2605_LRA_OPENLOOP_OLP;
#elif DRV2605_ACTUATOR_LRA && DRV2605_LRA_OPENLOOP_HZ
    return drv2605_hz_to_ol_period(DRV2605_LRA_OPENLOOP_HZ);
#else
    return 0;
#endif
}

void drv2605_lra_set_open_loop(uint8_t on, uint8_t olp)
{
#if DRV2605_ACTUATOR_LRA
    uint8_t c3;
    if (drv2605_read_reg(DRV2605_REG_CONTROL3, &c3) != I2C_OK) return;
    if (on) c3 |=  DRV2605_LRA_OPEN_LOOP;
    else    c3 &= (uint8_t)~DRV2605_LRA_OPEN_LOOP;
    drv2605_write_reg(DRV2605_REG_CONTROL3, c3);
    if (on && olp) drv2605_write_reg(DRV2605_REG_OL_LRA_PER, olp);
#else
    (void)on; (void)olp;
#endif
}

void drv2605_lra_configure(void)
{
    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);   /* leave standby */
    drv2605_write_reg(DRV2605_REG_CLAMPV, DRV2605_OD_CLAMP);

#if DRV2605_ACTUATOR_LRA
    {
        uint8_t fb;
        if (drv2605_read_reg(DRV2605_REG_FEEDBACK, &fb) == I2C_OK) {
            fb = (uint8_t)((fb & 0x7Fu) | DRV2605_N_ERM_LRA(1));
            drv2605_write_reg(DRV2605_REG_FEEDBACK, fb);
        }
    }
    drv2605_lra_set_open_loop(1, drv2605_effective_olp());
#else
    {
        uint8_t c3;
        if (drv2605_read_reg(DRV2605_REG_CONTROL3, &c3) == I2C_OK)
            drv2605_write_reg(DRV2605_REG_CONTROL3, (uint8_t)(c3 | DRV2605_ERM_OPEN_LOOP));
    }
#endif

    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_RTP);
    drv2605_write_reg(DRV2605_REG_RTP, 0x00);
    s_rtp_cache = 0x00;
    s_rtp_primed = 1;
}

i2c_status_t drv2605_set_amplitude(int8_t v)
{
    uint8_t raw = (uint8_t)v;
    if (s_rtp_primed && raw == s_rtp_cache) return I2C_OK;
    i2c_status_t st = drv2605_write_reg(DRV2605_REG_RTP, raw);
    if (st == I2C_OK) { s_rtp_cache = raw; s_rtp_primed = 1; }
    return st;
}

void drv2605_rtp_open(void)
{
#if DRV2605_ACTUATOR_LRA
    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
    drv2605_write_reg(DRV2605_REG_CLAMPV, DRV2605_OD_CLAMP);
    drv2605_lra_set_open_loop(1, drv2605_effective_olp());
    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_RTP);
    drv2605_write_reg(DRV2605_REG_RTP, 0x00);
    s_rtp_cache = 0x00;
    s_rtp_primed = 1;
#endif
}

void drv2605_rtp_level(uint8_t lvl)
{
    /* Uncached direct write — keeps the bench harness output byte-identical. */
    (void)drv2605_write_reg(DRV2605_REG_RTP, lvl);
    s_rtp_primed = 0;
}

void drv2605_rtp_close(void)
{
    drv2605_write_reg(DRV2605_REG_RTP, 0x00);
    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
    s_rtp_primed = 0;
}
