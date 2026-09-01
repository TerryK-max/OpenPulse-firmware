/******************************************************************************
 * log.h — logging facade.
 *
 * Phase 0: forwards to the existing USB CDC-ACM logger (src/usb_log.c). The
 * output is byte-identical to the old direct USB_Log_* calls.
 *
 * Later (Phase 3) log.c gains log_set_sink() so the same calls can target a
 * LOG protocol frame, the PA7 debug UART, or the RF back-channel — see
 * docs/ARCHITECTURE.md §2 and §"logging".
 *
 * RULE: never call from an ISR (docs/README.md rule 4). ISRs bump counters.
 *****************************************************************************/
#ifndef LOG_H
#define LOG_H

#include <stdint.h>
#include "usb_log.h"

typedef enum { LOG_ERR = 0, LOG_WARN = 1, LOG_INFO = 2, LOG_DBG = 3 } log_level_t;

/* Compile-time floor. Messages above this level are dropped by the LOGx macros. */
#ifndef LOG_MIN_LEVEL
#define LOG_MIN_LEVEL  LOG_DBG
#endif

void    log_init(void);            /* = USB_Log_Init()                 */
uint8_t log_is_connected(void);    /* = USB_Log_IsConnected()          */

/* Raw, no level tag — used by the bench harness to keep its exact output. */
#define log_puts(s)      USB_Log_Print((s))
#define log_printf(...)  USB_Log_Printf(__VA_ARGS__)

/* Levelled. Phase 0: no prefix is added (output unchanged); only the
 * compile-time LOG_MIN_LEVEL filter applies. */
#define LOG_AT(lvl, ...)  do { if ((lvl) <= LOG_MIN_LEVEL) USB_Log_Printf(__VA_ARGS__); } while (0)
#define LOGE(...)  LOG_AT(LOG_ERR,  __VA_ARGS__)
#define LOGW(...)  LOG_AT(LOG_WARN, __VA_ARGS__)
#define LOGI(...)  LOG_AT(LOG_INFO, __VA_ARGS__)
#define LOGD(...)  LOG_AT(LOG_DBG,  __VA_ARGS__)

#endif /* LOG_H */
