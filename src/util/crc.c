#include "util/crc.h"
#include "link/proto.h"

uint8_t crc8_smbus(const uint8_t *data, uint16_t len)
{
    uint8_t c = 0x00;
    while (len--) {
        c ^= *data++;
        for (int i = 0; i < 8; i++)
            c = (c & 0x80u) ? (uint8_t)((c << 1) ^ 0x07u) : (uint8_t)(c << 1);
    }
    return c;
}

/* proto.h declares this as the frame CRC entry point; keep it a thin alias so
 * there is exactly one implementation. */
uint8_t proto_crc8(const uint8_t *p, uint16_t n)
{
    return crc8_smbus(p, n);
}
