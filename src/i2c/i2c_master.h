/******************************************************************************
 * i2c_master.h — CH570D blocking I2C master (polling, timeouts, NACK detect,
 * bus-stuck recovery). No interrupts.
 *
 * Lifted from the bring-up in src/Main.c. Mirrors the CH57x reference flow in
 * EVT/EXAM/I2C/src/Main.c (START/ADDR/TxE/BTF/STOP, single-byte reception per
 * EVT/DOCS/modules/12_i2c.md §12.3) but never hangs.
 *
 * Phase 6 may add i2c_write_async() (IRQ-driven) behind the same header.
 *****************************************************************************/
#ifndef I2C_MASTER_H
#define I2C_MASTER_H

#include <stdint.h>

typedef enum {
    I2C_OK = 0,
    I2C_ERR_BUS_BUSY,
    I2C_ERR_TIMEOUT,
    I2C_ERR_NACK,       /* no ACK -> device absent, wrong address, or held EN */
} i2c_status_t;

const char *i2c_status_str(i2c_status_t s);

/**
 * @brief Initialise the I2C peripheral as a 7-bit master.
 *        Uses the default CH57x pin mapping: SCL/PA8, SDA/PA9
 *        (R16_PIN_ALTERNATE_H RB_I2C_PIN = 00). Enables the internal pull-ups.
 * @param hz  bus clock, e.g. 400000
 */
void i2c_master_init(uint32_t hz);

/** Recover a bus left stuck busy (SDA held low): peripheral soft-reset + re-init. */
void i2c_master_recover(void);

/** Write @p len bytes starting at register @p reg of 8-bit address @p addr8. */
i2c_status_t i2c_write(uint8_t addr8, uint8_t reg, const uint8_t *data, uint8_t len);

/** Convenience: write one register. */
i2c_status_t i2c_write_reg(uint8_t addr8, uint8_t reg, uint8_t val);

/** Read one register (write pointer, repeated START, read 1 byte + NACK + STOP). */
i2c_status_t i2c_read_reg(uint8_t addr8, uint8_t reg, uint8_t *val);

#endif /* I2C_MASTER_H */
