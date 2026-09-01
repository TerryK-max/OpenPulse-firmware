/******************************************************************************
 * crc.h — CRC-8/SMBUS (poly 0x07, init 0x00, no reflection, no final XOR).
 *
 * Used by the link layer on every frame (docs/PROTOCOL.md §2) and, later, by
 * the RF transport. Bitwise, table-free: a 60-byte frame is ~480 iterations,
 * run once per frame from the main loop — cost is negligible next to one I2C
 * write. No dynamic state, reentrant.
 *****************************************************************************/
#ifndef UTIL_CRC_H
#define UTIL_CRC_H

#include <stdint.h>

/** CRC-8/SMBUS of @p len bytes at @p data. Check value for "123456789" = 0xF4. */
uint8_t crc8_smbus(const uint8_t *data, uint16_t len);

#endif /* UTIL_CRC_H */
