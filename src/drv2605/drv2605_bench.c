#include "CH57x_common.h"
#include "config.h"

#if DRV2605_BENCH_TOOLS

#include "log/log.h"
#include "i2c/i2c_master.h"
#include "drv2605/drv2605.h"
#include "drv2605/drv2605_lra.h"
#include "drv2605/drv2605_bench.h"

/* ========================================================================
 *  EN pin + probe + reset
 * ======================================================================== */

void drv2605_en_high(void)
{
#ifdef DRV2605_EN_HARDWIRED
    log_puts("[DRV2605] EN assumed hard-wired to VDD\r\n");
#else
    GPIOA_ModeCfg(DRV2605_EN_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_SetBits(DRV2605_EN_PIN);
    log_printf("[DRV2605] EN driven HIGH on PA%d "
               "(adjust DRV2605_EN_PIN if your board differs)\r\n",
               __builtin_ctz(DRV2605_EN_PIN));
#endif
}

i2c_status_t drv2605_probe(void)
{
    i2c_status_t st = I2C_ERR_TIMEOUT;
    uint8_t status = 0;

    for (int attempt = 1; attempt <= 3; attempt++) {
        st = drv2605_read_reg(DRV2605_REG_STATUS, &status);
        log_printf("[DRV2605] probe attempt %d: read STATUS(0x00) -> %s",
                   attempt, i2c_status_str(st));
        if (st == I2C_OK) {
            log_printf("  raw=0x%02X\r\n", status);
            break;
        }
        log_puts("\r\n");
        if (st == I2C_ERR_BUS_BUSY) {
            log_puts("[DRV2605] bus stuck busy -> soft-reset + retry\r\n");
            i2c_master_recover();
        }
        mDelaymS(5);
    }
    if (st != I2C_OK) {
        log_puts("[DRV2605] ERROR: no answer on I2C.\r\n"
                 "          check: wiring (SCL/PA8, SDA/PA9), pull-ups,\r\n"
                 "          EN high, VDD 2.5-5.5 V, address 0x5A.\r\n");
        return st;
    }

    uint8_t devid = (status >> 5) & 0x07;
    log_printf("[DRV2605] DEVICE_ID = %u -> %s\r\n",
               devid, drv2605_devid_str(devid));
    log_printf("[DRV2605] STATUS: DIAG_RESULT=%u OVER_TEMP=%u OC_DETECT=%u\r\n",
               (status >> 3) & 1, (status >> 1) & 1, status & 1);
    return I2C_OK;
}

i2c_status_t drv2605_reset_and_wake(void)
{
    i2c_status_t st;

    log_puts("[DRV2605] DEV_RESET (MODE.7 = 1) ...\r\n");
    st = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_DEV_RESET);
    if (st) { log_printf("[DRV2605]   write failed: %s\r\n", i2c_status_str(st)); return st; }

    mDelaymS(2);
    for (int i = 0; i < 20; i++) {
        uint8_t mode;
        st = drv2605_read_reg(DRV2605_REG_MODE, &mode);
        if (st == I2C_OK && (mode & DRV2605_DEV_RESET) == 0) {
            log_printf("[DRV2605]   reset done, MODE(0x01)=0x%02X\r\n", mode);
            break;
        }
        mDelaymS(2);
    }

    log_puts("[DRV2605] leave STANDBY (MODE = 0x00) ...\r\n");
    st = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
    if (st) { log_printf("[DRV2605]   write failed: %s\r\n", i2c_status_str(st)); return st; }
    return I2C_OK;
}

/* ========================================================================
 *  Auto-calibration
 * ======================================================================== */

i2c_status_t drv2605_autocalibrate(void)
{
    i2c_status_t st;
    uint8_t fb, c1, c2, c3, c4, status;
    uint8_t comp = 0, bemf = 0, fb_after = 0;

    log_puts("\r\n--- Auto-calibration ---------------------------------\r\n");

    st = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_AUTOCAL);
    if (st) goto fail;
    log_printf("[CAL] MODE(0x01) <- 0x%02X (auto-calibration)\r\n",
               DRV2605_MODE_AUTOCAL);

    fb = DRV2605_FEEDBACK_CFG;
    st = drv2605_write_reg(DRV2605_REG_FEEDBACK, fb);
    if (st) goto fail;
    log_printf("[CAL] FEEDBACK_CONTROL(0x1A) <- 0x%02X "
               "(N_ERM_LRA=%u FB_BRAKE_FACTOR=%u LOOP_GAIN=%u BEMF_GAIN=%u)\r\n",
               fb, DRV2605_ACTUATOR_LRA, DRV2605_FB_BRAKE_FAC,
               DRV2605_LOOP_GAIN, DRV2605_BEMF_GAIN);

    st = drv2605_write_reg(DRV2605_REG_RATEDV, DRV2605_RATED_VOLTAGE);
    if (st) goto fail;
    st = drv2605_write_reg(DRV2605_REG_CLAMPV, DRV2605_OD_CLAMP);
    if (st) goto fail;
    log_printf("[CAL] RATED_VOLTAGE(0x16) <- 0x%02X, OD_CLAMP(0x17) <- 0x%02X\r\n",
               DRV2605_RATED_VOLTAGE, DRV2605_OD_CLAMP);

    st = drv2605_read_reg(DRV2605_REG_CONTROL1, &c1);
    if (st) goto fail;
    c1 = (uint8_t)((c1 & ~0x1Fu) | (DRV2605_DRIVE_TIME & 0x1Fu));
    st = drv2605_write_reg(DRV2605_REG_CONTROL1, c1);
    if (st) goto fail;
    log_printf("[CAL] CONTROL1(0x1B) <- 0x%02X (DRIVE_TIME=%u)\r\n",
               c1, DRV2605_DRIVE_TIME);

    st = drv2605_read_reg(DRV2605_REG_CONTROL3, &c3);
    if (st) goto fail;
#if DRV2605_ACTUATOR_LRA
    c3 &= (uint8_t)~DRV2605_LRA_OPEN_LOOP;      /* auto-resonance during cal */
#else
    c3 |= DRV2605_ERM_OPEN_LOOP;
#endif
    st = drv2605_write_reg(DRV2605_REG_CONTROL3, c3);
    if (st) goto fail;

    st = drv2605_read_reg(DRV2605_REG_CONTROL4, &c4);
    if (st) goto fail;
    c4 = (uint8_t)((c4 & ~(3u << 4)) | DRV2605_AUTO_CAL_TIME_BITS);
    st = drv2605_write_reg(DRV2605_REG_CONTROL4, c4);
    if (st) goto fail;

    (void)drv2605_read_reg(DRV2605_REG_CONTROL2, &c2);
    log_printf("[CAL] readback 0x1A=0x%02X 0x1B=0x%02X 0x1C=0x%02X 0x1D=0x%02X 0x1E=0x%02X\r\n",
               fb, c1, c2, c3, c4);

    for (uint8_t attempt = 1; attempt <= DRV2605_CAL_ATTEMPTS; attempt++) {
        st = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_AUTOCAL);
        if (st) goto fail;
        st = drv2605_write_reg(DRV2605_REG_GO, DRV2605_GO_BIT);
        if (st) goto fail;
        log_printf("[CAL] attempt %u/%u: GO <- 1, calibrating (motor moves) ...\r\n",
                   attempt, DRV2605_CAL_ATTEMPTS);

        st = drv2605_wait_go_clear(2000);
        if (st) {
            log_printf("[CAL] GO did not clear: %s\r\n", i2c_status_str(st));
            return st;
        }

        st = drv2605_read_reg(DRV2605_REG_STATUS, &status);
        if (st) goto fail;
        (void)drv2605_read_reg(DRV2605_REG_ACAL_COMP, &comp);
        (void)drv2605_read_reg(DRV2605_REG_ACAL_BEMF, &bemf);
        (void)drv2605_read_reg(DRV2605_REG_FEEDBACK,  &fb_after);

        log_printf("[CAL] STATUS(0x00)=0x%02X  DIAG_RESULT=%u  FB_STS=%u  OC_DETECT=%u\r\n",
                   status, (status >> 3) & 1, (status >> 2) & 1, status & 1);
        log_printf("[CAL] A_CAL_COMP(0x18)=0x%02X  A_CAL_BEMF(0x19)=0x%02X  "
                   "BEMF_GAIN=%u\r\n", comp, bemf, fb_after & 0x03);

        if ((status & DRV2605_STATUS_DIAG) == 0) {
            log_puts("[CAL] RESULT: PASSED (optimum result converged)\r\n");
            log_puts("-----------------------------------------------------\r\n");
            return I2C_OK;
        }
        log_printf("[CAL] attempt %u did not converge%s\r\n", attempt,
                   (attempt < DRV2605_CAL_ATTEMPTS) ? " - retrying" : "");
        mDelaymS(50);
    }

    log_puts("[CAL] RESULT: FAILED after all attempts.\r\n");
#if DRV2605_ACTUATOR_LRA
    log_puts("      FB_STS=1 => the auto-resonance engine never locked.\r\n"
             "      Check, in order:\r\n"
             "        1. OUT+/OUT- really wired to the LRA coil\r\n"
             "           (Taptic Engine flex also carries a Hall sensor)\r\n"
             "        2. DRIVE_TIME vs the resonance measured below\r\n"
             "        3. raise OD_CLAMP and/or VDD current headroom\r\n"
             "      NOTE: LRA auto-resonance PLAYBACK does not need calibration\r\n"
             "      (EVT/DRV2605_interface.md, 'Auto-Resonance Engine for LRA'),\r\n"
             "      so the effect test still runs - only level/brake accuracy\r\n"
             "      is affected.\r\n");
#else
    log_puts("      Check actuator wiring / RATED_VOLTAGE / OD_CLAMP / type.\r\n");
#endif
    return I2C_ERR_TIMEOUT;

fail:
    log_printf("[CAL] I2C error during calibration setup: %s\r\n",
               i2c_status_str(st));
    return st;
}

/* ========================================================================
 *  Playback + resonance diagnostics
 * ======================================================================== */

i2c_status_t drv2605_play_effect(uint8_t effect_id)
{
    i2c_status_t st;

    st = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
    if (st) goto fail;

#if (DRV2605_ACTUATOR_LRA && DRV2605_OL_PINNED)
    drv2605_lra_set_open_loop(1, drv2605_effective_olp());
#endif

    st = drv2605_write_reg(DRV2605_REG_LIBRARY, DRV2605_LIBRARY);
    if (st) goto fail;

    st = drv2605_write_reg(DRV2605_REG_WAVESEQ1, effect_id & 0x7F);
    if (st) goto fail;
    st = drv2605_write_reg(DRV2605_REG_WAVESEQ2, 0x00);
    if (st) goto fail;

    log_printf("[PLAY] library=%u, WAVESEQ1(0x04)=%u -> GO\r\n",
               DRV2605_LIBRARY, effect_id);

    st = drv2605_write_reg(DRV2605_REG_GO, DRV2605_GO_BIT);
    if (st) goto fail;

    st = drv2605_wait_go_clear(1500);
    if (st) {
        log_printf("[PLAY] GO did not clear: %s\r\n", i2c_status_str(st));
        return st;
    }
    log_puts("[PLAY] effect finished (GO self-cleared)\r\n");
    return I2C_OK;

fail:
    log_printf("[PLAY] I2C error: %s\r\n", i2c_status_str(st));
    return st;
}

void drv2605_measure_resonance(void)
{
#if DRV2605_ACTUATOR_LRA
    uint8_t p = 0, st1 = 0;
    uint8_t vmin = 0xFF, vmax = 0, vlast = 0;
    uint16_t nvalid = 0;

    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_RTP);
    drv2605_write_reg(DRV2605_REG_RTP, 0x7F);

    for (int i = 0; i < 75; i++) {          /* ~600 ms */
        if (drv2605_read_reg(DRV2605_REG_LRA_PERIOD, &p) == I2C_OK && p) {
            if (p < vmin) vmin = p;
            if (p > vmax) vmax = p;
            vlast = p;
            nvalid++;
        }
        mDelaymS(8);
    }

    drv2605_write_reg(DRV2605_REG_RTP, 0x00);
    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
    (void)drv2605_read_reg(DRV2605_REG_STATUS, &st1);

    if (nvalid == 0) {
        log_puts("[LRA] reg0x22 never updated -> the DRV2605 is NOT sensing "
                 "an actuator.\r\n"
                 "      Verify OUT+/OUT- are on the LRA DRIVE coil (the iPhone\r\n"
                 "      Taptic Engine FPC also has a Hall-sensor coil).\r\n");
        return;
    }

    {
        uint32_t per_lo = (uint32_t)vmin * 9846u / 100u;
        uint32_t per_hi = (uint32_t)vmax * 9846u / 100u;
        uint32_t per_l  = (uint32_t)vlast * 9846u / 100u;
        uint32_t f_hi   = 1000000000u / per_lo;
        uint32_t f_lo   = 1000000000u / per_hi;
        uint32_t dt_opt = (per_l > 1200u && per_l / 200u > 5u) ? (per_l / 200u - 5u) : 0u;

        uint32_t f_l3 = f_lo * 3u;

        log_printf("[LRA] reg0x22 over %u samples: min=%u max=%u last=%u  "
                   "STATUS=0x%02X FB_STS=%u\r\n",
                   nvalid, vmin, vmax, vlast, st1, (st1 >> 2) & 1);
        log_printf("[LRA] naive f in [%lu.%03lu .. %lu.%03lu] Hz "
                   "(last period ~%lu us)\r\n",
                   f_lo / 1000u, f_lo % 1000u, f_hi / 1000u, f_hi % 1000u, per_l);

        if ((st1 >> 2) & 1) {
            log_printf("[LRA] FB_STS=1 => NOT locked. If this is a subharmonic "
                       "miscount the real f ~ %lu.%03lu Hz (x2) or "
                       "%lu.%03lu Hz (x3).\r\n",
                       (f_lo * 2u) / 1000u, (f_lo * 2u) % 1000u,
                       f_l3 / 1000u, f_l3 % 1000u);
            log_puts("[LRA] Trust the open-loop sweep below, not this number.\r\n");
        } else if (dt_opt >= 1u && dt_opt <= 31u) {
            log_printf("[LRA] locked. => set DRV2605_DRIVE_TIME = %lu "
                       "(currently %u) and re-run calibration\r\n",
                       dt_opt, DRV2605_DRIVE_TIME);
        }
    }
#endif
}

void drv2605_lra_open_loop_sweep(void)
{
#if DRV2605_ACTUATOR_LRA
    log_printf("\r\n[SWEEP] open-loop drive %d -> %d Hz, step %d Hz, ~0.8 s each, "
               "OD_CLAMP=0x%02X RTP=0x%02X.\r\n"
               "        Note (by feel) which step buzzes HARDEST = resonance.\r\n",
               DRV2605_SWEEP_HZ_HI, DRV2605_SWEEP_HZ_LO, DRV2605_SWEEP_HZ_STEP,
               DRV2605_OD_CLAMP, DRV2605_RTP_LEVEL);

    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
    drv2605_write_reg(DRV2605_REG_CLAMPV, DRV2605_OD_CLAMP);
    drv2605_lra_set_open_loop(1, 0);
    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_RTP);

    {
        uint8_t prev_olp = 0;
        int idx = 0;
        for (int hz = DRV2605_SWEEP_HZ_HI; hz >= DRV2605_SWEEP_HZ_LO;
             hz -= DRV2605_SWEEP_HZ_STEP) {
            uint8_t olp = drv2605_hz_to_ol_period((uint16_t)hz);
            uint32_t real_mhz = drv2605_olp_to_mhz(olp);
            uint8_t vb = 0;
            if (olp == prev_olp)
                continue;
            prev_olp = olp;
            drv2605_write_reg(DRV2605_REG_OL_LRA_PER, olp);
            drv2605_write_reg(DRV2605_REG_RTP, DRV2605_RTP_LEVEL);
            mDelaymS(600);
            drv2605_read_reg(DRV2605_REG_VBAT, &vb);
            log_printf("[SWEEP] #%2d  target %3d Hz -> actual %lu.%03lu Hz "
                       "(reg0x20=%u)  VBAT~%lumV\r\n",
                       ++idx, hz, real_mhz / 1000u, real_mhz % 1000u, olp,
                       (uint32_t)vb * 5600u / 255u);
            mDelaymS(200);
            drv2605_write_reg(DRV2605_REG_RTP, 0x00);
            mDelaymS(250);
        }
    }

    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
#if !DRV2605_OL_PINNED
    drv2605_lra_set_open_loop(0, 0);
#endif
    log_puts("[SWEEP] done -> set DRV2605_LRA_OPENLOOP_OLP to the reg0x20 of "
             "the loudest step and re-flash.\r\n");
#endif
}

#if (DRV2605_ACTUATOR_LRA && DRV2605_RUN_MAX_TEST)
void drv2605_max_drive_test(void)
{
    uint8_t olp = drv2605_effective_olp();
    uint8_t vb = 0, s = 0;

    if (!olp) { log_puts("[MAX] no open-loop frequency pinned - skipped\r\n"); return; }

    log_printf("\r\n[MAX] open-loop RTP: reg0x20=%u (~%lu Hz), OD_CLAMP=0x%02X, "
               "RTP=0x%02X, 2 s\r\n", olp, drv2605_olp_to_mhz(olp) / 1000u,
               DRV2605_OD_CLAMP, DRV2605_RTP_LEVEL);

    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
    drv2605_write_reg(DRV2605_REG_CLAMPV, DRV2605_OD_CLAMP);
    drv2605_lra_set_open_loop(1, olp);
    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_RTP);
    drv2605_write_reg(DRV2605_REG_RTP, DRV2605_RTP_LEVEL);

    for (int i = 0; i < 8; i++) {
        mDelaymS(250);
        vb = 0; s = 0;
        drv2605_read_reg(DRV2605_REG_VBAT, &vb);
        drv2605_read_reg(DRV2605_REG_STATUS, &s);
        log_printf("[MAX]  t=%4dms  VBAT=0x%02X (~%lumV)  STATUS=0x%02X "
                   "OVER_TEMP=%u OC_DETECT=%u\r\n",
                   (i + 1) * 250, vb, (uint32_t)vb * 5600u / 255u, s,
                   (s >> 1) & 1, s & 1);
    }

    drv2605_write_reg(DRV2605_REG_RTP, 0x00);
    drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
    log_puts("[MAX] VBAT steady + no OC/OVER_TEMP -> driver has more to give: "
             "raise OD_CLAMP toward 0xFF / check you are on resonance.\r\n"
             "      VBAT sags hard or OC/OVER_TEMP set -> supply or the\r\n"
             "      DRV2605L output stage is the ceiling (it is the low-power\r\n"
             "      variant; a phone Taptic Engine wants a bigger amp).\r\n");
}
#endif

#endif /* DRV2605_BENCH_TOOLS */
