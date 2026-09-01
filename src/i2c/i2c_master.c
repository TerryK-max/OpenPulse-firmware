#include "CH57x_common.h"
#include "i2c/i2c_master.h"

#define I2C_SPINS   0x00080000u   /* generous per-phase spin budget */

static uint32_t s_hz = 400000;    /* remembered for i2c_master_recover() */

const char *i2c_status_str(i2c_status_t s)
{
    switch (s) {
        case I2C_OK:            return "OK";
        case I2C_ERR_BUS_BUSY:  return "BUS_BUSY";
        case I2C_ERR_TIMEOUT:   return "TIMEOUT";
        case I2C_ERR_NACK:      return "NACK";
        default:                return "?";
    }
}

static i2c_status_t wait_not_busy(void)
{
    uint32_t t = I2C_SPINS;
    while (I2C_GetFlagStatus(I2C_FLAG_BUSY) != RESET) {
        if (--t == 0) return I2C_ERR_BUS_BUSY;
    }
    return I2C_OK;
}

/* Spin until STAR1 has 'bit' set. Abort on AF (acknowledge failure = NACK). */
static i2c_status_t wait_sr1(uint16_t bit)
{
    uint32_t t = I2C_SPINS;
    for (;;) {
        uint16_t sr1 = R16_I2C_STAR1;
        if (sr1 & bit) return I2C_OK;
        if (sr1 & RB_I2C_AF) {
            I2C_ClearFlag(I2C_FLAG_AF);
            I2C_GenerateSTOP(ENABLE);
            return I2C_ERR_NACK;
        }
        if (--t == 0) {
            I2C_GenerateSTOP(ENABLE);
            return I2C_ERR_TIMEOUT;
        }
    }
}

static inline void clear_addr(void)
{
    volatile uint16_t tmp = R16_I2C_STAR1;
    tmp = R16_I2C_STAR2;
    (void)tmp;
}

void i2c_master_init(uint32_t hz)
{
    s_hz = hz;

    /* Default I2C pin mapping = SCL/PA8, SDA/PA9 (EVT/DOCS/modules/07_gpio.md,
     * R16_PIN_ALTERNATE_H RB_I2C_PIN = 00). The 2-wire debug interface that
     * shares PA8/PA9 is disabled in board_init() (RB_PIN_DEBUG_EN cleared). */
    R16_PIN_ALTERNATE_H &= ~RB_I2C_PIN;

    /* I2C is open-drain; enable the internal pull-ups. External 2.2 kO pull-ups
     * to VDD are still recommended (DRV2605 datasheet). */
    GPIOA_ModeCfg(GPIO_Pin_8 | GPIO_Pin_9, GPIO_ModeIN_PU);

    I2C_Init(I2C_Mode_I2C, hz, I2C_DutyCycle_16_9,
             I2C_Ack_Enable, I2C_AckAddr_7bit, 0x42);
}

void i2c_master_recover(void)
{
    I2C_SoftwareResetCmd(ENABLE);
    mDelaymS(1);
    I2C_SoftwareResetCmd(DISABLE);
    i2c_master_init(s_hz);
}

i2c_status_t i2c_write(uint8_t addr8, uint8_t reg, const uint8_t *data, uint8_t len)
{
    i2c_status_t st;

    st = wait_not_busy();
    if (st) return st;

    I2C_GenerateSTART(ENABLE);
    st = wait_sr1(RB_I2C_SB);                     /* EV5 */
    if (st) return st;

    I2C_Send7bitAddress(addr8, I2C_Direction_Transmitter);
    st = wait_sr1(RB_I2C_ADDR);                   /* EV6 (or NACK) */
    if (st) return st;
    clear_addr();

    st = wait_sr1(RB_I2C_TxE);
    if (st) return st;
    I2C_SendData(reg);

    for (uint8_t i = 0; i < len; i++) {
        st = wait_sr1(RB_I2C_TxE);
        if (st) return st;
        I2C_SendData(data[i]);
    }

    st = wait_sr1(RB_I2C_BTF);                    /* EV8_2 */
    if (st) return st;
    I2C_GenerateSTOP(ENABLE);
    return I2C_OK;
}

i2c_status_t i2c_write_reg(uint8_t addr8, uint8_t reg, uint8_t val)
{
    return i2c_write(addr8, reg, &val, 1);
}

i2c_status_t i2c_read_reg(uint8_t addr8, uint8_t reg, uint8_t *val)
{
    i2c_status_t st;
    uint32_t irqv;
    uint32_t t;

    st = wait_not_busy();
    if (st) return st;

    /* --- phase 1: address + register pointer, no STOP --- */
    I2C_GenerateSTART(ENABLE);
    st = wait_sr1(RB_I2C_SB);
    if (st) return st;
    I2C_Send7bitAddress(addr8, I2C_Direction_Transmitter);
    st = wait_sr1(RB_I2C_ADDR);
    if (st) return st;
    clear_addr();
    st = wait_sr1(RB_I2C_TxE);
    if (st) return st;
    I2C_SendData(reg);
    st = wait_sr1(RB_I2C_BTF);
    if (st) return st;

    /* --- phase 2: repeated START + single-byte read --- */
    I2C_AcknowledgeConfig(DISABLE);          /* NACK the (only) byte */
    I2C_GenerateSTART(ENABLE);
    st = wait_sr1(RB_I2C_SB);
    if (st) { I2C_AcknowledgeConfig(ENABLE); return st; }
    I2C_Send7bitAddress(addr8, I2C_Direction_Receiver);
    st = wait_sr1(RB_I2C_ADDR);
    if (st) { I2C_AcknowledgeConfig(ENABLE); return st; }

    /* Clear ADDR then immediately request STOP so exactly one byte is clocked
     * in. Keep this window free of interrupts (USB ISR must not stretch it). */
    SYS_DisableAllIrq(&irqv);
    clear_addr();
    I2C_GenerateSTOP(ENABLE);
    SYS_RecoverIrq(irqv);

    st = wait_sr1(RB_I2C_RxNE);
    if (st) { I2C_AcknowledgeConfig(ENABLE); return st; }
    *val = I2C_ReceiveData();

    t = I2C_SPINS;                           /* let the STOP finish */
    while ((R16_I2C_CTRL1 & RB_I2C_STOP) && --t) { }
    I2C_AcknowledgeConfig(ENABLE);           /* restore default for next txn */
    return I2C_OK;
}
