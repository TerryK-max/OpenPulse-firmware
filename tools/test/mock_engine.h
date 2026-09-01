/******************************************************************************
 * mock_engine.h — host test double for src/haptic/haptic_engine.h.
 *
 * Implements the engine's producer/consumer API with plain in-memory state so
 * the link layer can be exercised off-target. The real SPSC FIFO
 * (src/haptic/haptic_fifo.h) is used unchanged. Tests inspect g_mock.
 *****************************************************************************/
#ifndef MOCK_ENGINE_H
#define MOCK_ENGINE_H

#include <stdint.h>
#include "haptic/haptic_engine.h"

typedef struct {
    uint32_t now_ms;               /* returned by haptic_now_ms(); test sets it */

    uint32_t notify_data_calls;
    uint32_t abort_calls;
    uint32_t apply_config_calls;
    uint32_t set_mode_calls;
    uint32_t set_envelope_calls;

    haptic_config_t last_cfg;      /* argument of the last haptic_apply_config() */
    haptic_mode_t   mode;          /* current (mock applies mode changes at once) */
    uint8_t         env_target;
    uint8_t         env_slew_ms;
} mock_engine_t;

extern mock_engine_t g_mock;

/* Convenience for assertions: pop the whole FIFO into buf, return the count. */
uint32_t mock_drain_fifo(int8_t *buf, uint32_t cap);

#endif /* MOCK_ENGINE_H */
