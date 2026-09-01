#include "CH57x_common.h"
#include "config.h"
#include "log/log.h"
#include "drv2605/drv2605.h"
#include "drv2605/drv2605_lra.h"
#include "haptic/haptic_patterns.h"

void haptic_pulse(uint8_t amp, uint16_t on_ms, uint8_t brake)
{
#if DRV2605_ACTUATOR_LRA
    uint8_t olp = drv2605_effective_olp();
    if (!olp) return;

    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
    drv2605_write_reg(DRV2605_REG_CLAMPV, DRV2605_OD_CLAMP);
    drv2605_lra_set_open_loop(1, olp);
    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_RTP);

    drv2605_write_reg(DRV2605_REG_RTP, amp);           /* drive */
    mDelaymS(on_ms);
    if (brake) {
        drv2605_write_reg(DRV2605_REG_RTP, 0x81);      /* reverse ~= brake */
        mDelaymS(6);
    }
    drv2605_write_reg(DRV2605_REG_RTP, 0x00);          /* stop */
    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
#else
    (void)amp; (void)on_ms; (void)brake;
#endif
}

int8_t haptic_pattern_simracing(uint32_t sample_idx, uint16_t sample_rate_hz)
{
    uint32_t rate = sample_rate_hz ? sample_rate_hz : 1000u;
    uint32_t ms   = (uint32_t)(((uint64_t)sample_idx * 1000u) / rate);

    int lvl = DRV2605_RUMBLE_AMP;
    int wob = (int)((ms / 250u) % 4u);                 /* slow wobble, ~1 s period */
    lvl += (wob == 3) ? -10 : (wob * 7);
    lvl += (int)(((ms / 10u) * 7u) % 11u) - 5;         /* fast fine texture */
    if ((ms % 1500u) < 30u) lvl = DRV2605_IMPACT_AMP;  /* ~30 ms kerb hit / 1.5 s */

    if (lvl < 0)    lvl = 0;
    if (lvl > 0x7F) lvl = 0x7F;
    return (int8_t)lvl;
}

void haptic_demo_simracing(uint16_t seconds)
{
#if DRV2605_ACTUATOR_LRA
    uint32_t ticks = (uint32_t)seconds * 100u;     /* 10 ms steps */
    uint32_t t;
    uint8_t  vb = 0, s = 0;

    if (!drv2605_effective_olp()) {
        log_puts("[SIM] no open-loop frequency pinned - skipped\r\n");
        return;
    }
    log_printf("[SIM] rumble=0x%02X impact=0x%02X OD_CLAMP=0x%02X @ ~%lu Hz, "
               "%us - watch the actuator temperature\r\n",
               DRV2605_RUMBLE_AMP, DRV2605_IMPACT_AMP, DRV2605_OD_CLAMP,
               drv2605_olp_to_mhz(drv2605_effective_olp()) / 1000u, seconds);

    drv2605_rtp_open();
    for (t = 0; t < ticks; t++) {
        int lvl = DRV2605_RUMBLE_AMP;
        int wob = (int)((t / 25u) % 4u);            /* slow 0..3 wobble */
        lvl += (wob == 3) ? -10 : (wob * 7);
        lvl += (int)((t * 7u) % 11u) - 5;           /* fast fine texture */
        if ((t % 150u) < 3u) lvl = DRV2605_IMPACT_AMP;   /* ~30 ms kerb hit */
        if (lvl < 0)    lvl = 0;
        if (lvl > 0x7F) lvl = 0x7F;
        drv2605_rtp_level((uint8_t)lvl);
        mDelaymS(10);

        if ((t % 200u) == 199u) {
            drv2605_read_reg(DRV2605_REG_VBAT,   &vb);
            drv2605_read_reg(DRV2605_REG_STATUS, &s);
            log_printf("[SIM] t=%2lus  VBAT~%lumV  STATUS=0x%02X OVER_TEMP=%u OC=%u\r\n",
                       t / 100u, (uint32_t)vb * 5600u / 255u, s,
                       (s >> 1) & 1, s & 1);
        }
    }
    drv2605_rtp_close();
    log_puts("[SIM] done. Too hot too fast -> lower DRV2605_RUMBLE_AMP or "
             "OD_CLAMP, or duty-cycle. Too weak -> raise RUMBLE_AMP first, "
             "then OD_CLAMP.\r\n");
#else
    (void)seconds;
#endif
}
