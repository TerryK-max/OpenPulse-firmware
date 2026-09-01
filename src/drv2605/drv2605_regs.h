/******************************************************************************
 * drv2605_regs.h — TI DRV2605L register map + bit definitions.
 *
 * Header only, no includes. Cross-check every entry against the TI DRV2605L
 * datasheet SLOS854C (§ = section there) — https://www.ti.com/lit/ds/symlink/drv2605l.pdf
 * ; see docs/vendor/README.md. Key values are also transcribed in docs/HARDWARE.md §2.2.
 * ⚠️ The chip on this board is the -L variant; EVT/DRV2605_interface.md is the
 * non-L datasheet and is WRONG for CONTROL5 / OL_LRA_PERIOD / field widths.
 *****************************************************************************/
#ifndef DRV2605_REGS_H
#define DRV2605_REGS_H

/* I2C address (7-bit 0x5A => 0xB4 write / 0xB5 read) — datasheet §7.5 */
#define DRV2605_ADDR7          0x5A
#define DRV2605_ADDR8          (DRV2605_ADDR7 << 1)   /* 0xB4 */

/* Register addresses — datasheet §8.6 */
#define DRV2605_REG_STATUS     0x00   /* DEVICE_ID[7:5] DIAG_RESULT[3] FB_STS[2] OVER_TEMP[1] OC_DETECT[0] */
#define DRV2605_REG_MODE       0x01   /* DEV_RESET[7] STANDBY[6] MODE[2:0] */
#define DRV2605_REG_RTP        0x02   /* RTP_INPUT[7:0] — the amplitude input (signed) */
#define DRV2605_REG_LIBRARY    0x03   /* LIBRARY_SEL[2:0] */
#define DRV2605_REG_WAVESEQ1   0x04   /* 0x04..0x0B: 8 sequencer slots */
#define DRV2605_REG_WAVESEQ2   0x05
#define DRV2605_REG_GO         0x0C   /* GO[0] — self-clearing */
#define DRV2605_REG_RATEDV     0x16   /* RATED_VOLTAGE[7:0] — closed-loop only */
#define DRV2605_REG_CLAMPV     0x17   /* OD_CLAMP[7:0] — open-loop full-scale / peak ref (§8.5.2.2) */
#define DRV2605_REG_ACAL_COMP  0x18   /* A_CAL_COMP[7:0] — cal output (L reset 0x0C) */
#define DRV2605_REG_ACAL_BEMF  0x19   /* A_CAL_BEMF[7:0] — cal output (L reset 0x6C) */
#define DRV2605_REG_FEEDBACK   0x1A   /* N_ERM_LRA[7] FB_BRAKE_FACTOR[6:4] LOOP_GAIN[3:2] BEMF_GAIN[1:0] */
#define DRV2605_REG_CONTROL1   0x1B   /* STARTUP_BOOST[7] AC_COUPLE[5] DRIVE_TIME[4:0] */
#define DRV2605_REG_CONTROL2   0x1C   /* SAMPLE_TIME[5:4] BLANKING_TIME[3:2] IDISS_TIME[1:0] */
#define DRV2605_REG_CONTROL3   0x1D   /* ERM_OPEN_LOOP[5] ... LRA_OPEN_LOOP[0] */
#define DRV2605_REG_CONTROL4   0x1E   /* ZC_DET_TIME[7:6] AUTO_CAL_TIME[5:4] (L) */
#define DRV2605_REG_CONTROL5   0x1F   /* L only: AUTO_OL_CNT[7:6] LRA_AUTO_OPEN_LOOP[5] ... */
#define DRV2605_REG_OL_LRA_PER 0x20   /* L only: OL_LRA_PERIOD[6:0], period_us = val x 98.46 (§8.6.26) */
#define DRV2605_REG_VBAT       0x21   /* VBAT[7:0], VDD = val x 5.6/255 during drive (§8.6.27) */
#define DRV2605_REG_LRA_PERIOD 0x22   /* LRA_PERIOD[7:0], period_us = val x 98.46 (§8.6.28) */

/* MODE[2:0] values (reg 0x01) — datasheet §8.6.2 */
#define DRV2605_MODE_INTTRIG   0x00   /* internal trigger */
#define DRV2605_MODE_RTP       0x05   /* real-time playback: drive = reg 0x02 */
#define DRV2605_MODE_AUTOCAL   0x07   /* auto calibration */
#define DRV2605_MODE_STANDBY   0x40   /* STANDBY bit */
#define DRV2605_DEV_RESET      0x80   /* DEV_RESET bit */

/* Bits */
#define DRV2605_GO_BIT         0x01
#define DRV2605_STATUS_DIAG    0x08   /* DIAG_RESULT: 0 = pass, 1 = fail */
#define DRV2605_N_ERM_LRA(lra) (((lra) & 1u) << 7)   /* FEEDBACK_CONTROL bit 7 */
#define DRV2605_ERM_OPEN_LOOP  (1u << 5)             /* CONTROL3 bit 5 */
#define DRV2605_LRA_OPEN_LOOP  (1u << 0)             /* CONTROL3 bit 0 */

#endif /* DRV2605_REGS_H */
