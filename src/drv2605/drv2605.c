#include "CH57x_common.h"
#include "config.h"
#include "drv2605/drv2605.h"

i2c_status_t drv2605_wait_go_clear(uint16_t timeout_ms)
{
    while (1) {
        uint8_t go;
        i2c_status_t st = drv2605_read_reg(DRV2605_REG_GO, &go);
        if (st) return st;
        if ((go & DRV2605_GO_BIT) == 0) return I2C_OK;
        if (timeout_ms == 0) return I2C_ERR_TIMEOUT;
        mDelaymS(10);
        timeout_ms = (timeout_ms > 10) ? (uint16_t)(timeout_ms - 10) : 0;
    }
}

const char *drv2605_devid_str(uint8_t id)
{
    switch (id) {
        case 3: return "DRV2605 (ROM library, no RAM)";
        case 4: return "DRV2604 (RAM, no ROM library)";
        case 6: return "DRV2604L";
        case 7: return "DRV2605L";
        default: return "unknown";
    }
}

void drv2605_en_assert(void)
{
#ifndef DRV2605_EN_HARDWIRED
    GPIOA_ModeCfg(DRV2605_EN_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_SetBits(DRV2605_EN_PIN);
#endif
}

i2c_status_t drv2605_reset(void)
{
    i2c_status_t st = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_DEV_RESET);
    if (st) return st;

    mDelaymS(2);
    for (int i = 0; i < 20; i++) {
        uint8_t m;
        if (drv2605_read_reg(DRV2605_REG_MODE, &m) == I2C_OK &&
            (m & DRV2605_DEV_RESET) == 0) {
            break;
        }
        mDelaymS(2);
    }
    return drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
}
