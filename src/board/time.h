/******************************************************************************
 * time.h — timing.
 *
 *   time_delay_ms/us   busy delays (WCH NOP loops in libISP572.a — they do NOT
 *                      use SysTick, so the SysTick tick below is free to run).
 *   time_tick_start()  starts the fixed-rate SAMPLE CLOCK on the core SysTick
 *                      (SysTick_IRQn = 12). The ISR only increments a counter.
 *   time_ticks()       raw tick count since time_tick_start() — the main loop
 *                      compares this to what it has serviced to pace haptic_tick()
 *                      and to measure backlog (docs/ARCHITECTURE.md §3).
 *   time_now_ms()      milliseconds since time_tick_start() — for the failsafe.
 *****************************************************************************/
#ifndef TIME_H
#define TIME_H

#include <stdint.h>
#include "CH57x_common.h"

#define time_delay_ms(ms)   mDelaymS((ms))
#define time_delay_us(us)   mDelayuS((us))
#define time_sysclock_hz()  GetSysClock()

/** Start the SysTick sample clock at @p hz. Call once, after board_init(). */
void     time_tick_start(uint32_t hz);

/** Raw tick count since time_tick_start(). Wraps after ~49 days at 1 kHz. */
uint32_t time_ticks(void);

/** Milliseconds since time_tick_start(). */
uint32_t time_now_ms(void);

#endif /* TIME_H */
