/******************************************************************************
 * log.h — logging facade.
 *
 * Forwards to the CDC-ACM channel of the composite USB device
 * (src/usb/usb_cdc.h). Human-readable text only; the framed protocol has its
 * own path (docs/PROTOCOL.md, src/link/).
 *
 * Phase 3.3 will add log_set_sink() so the same calls can target a TYPE_LOG
 * protocol frame or the PA7 debug UART — see docs/ARCHITECTURE.md §2.
 *
 * RULE: never call from an ISR (docs/README.md rule 4). ISRs bump counters.
 *****************************************************************************/
#ifndef LOG_H
#define LOG_H

#include <stdint.h>
#include "usb/usb_cdc.h"

typedef enum { LOG_ERR = 0, LOG_WARN = 1, LOG_INFO = 2, LOG_DBG = 3 } log_level_t;

/* Compile-time floor. Messages above this level are dropped by the LOGx macros. */
#ifndef LOG_MIN_LEVEL
#define LOG_MIN_LEVEL  LOG_DBG
#endif

void    log_init(void);            /* = usb_device_init()  */
uint8_t log_is_connected(void);    /* = usb_device_configured() */

/* Raw, no level tag. */
#define log_puts(s)      usb_cdc_write((s))
#define log_printf(...)  usb_cdc_printf(__VA_ARGS__)

/* Levelled. Phase 3.3: no prefix added yet; only the compile-time filter. */
#define LOG_AT(lvl, ...)  do { if ((lvl) <= LOG_MIN_LEVEL) usb_cdc_printf(__VA_ARGS__); } while (0)
#define LOGE(...)  LOG_AT(LOG_ERR,  __VA_ARGS__)
#define LOGW(...)  LOG_AT(LOG_WARN, __VA_ARGS__)
#define LOGI(...)  LOG_AT(LOG_INFO, __VA_ARGS__)
#define LOGD(...)  LOG_AT(LOG_DBG,  __VA_ARGS__)

#endif /* LOG_H */
