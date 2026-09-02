#include "CH57x_common.h"
#include "core_riscv.h"
#include "board/time.h"

/* s_ticks is free-running and monotonic for the whole session — it is NEVER
 * reset, so callers that pace on time_ticks() (the main loop's produced/serviced
 * counters) are never thrown backwards when the sample rate changes at runtime.
 * A rate change only re-bases the ms conversion so time_now_ms() stays
 * monotonic across it. */
static volatile uint32_t s_ticks;
static uint32_t s_tick_hz     = 1000;
static uint32_t s_ms_base     = 0;   /* time_now_ms() at the last rate change   */
static uint32_t s_ticks_base  = 0;   /* s_ticks at the last rate change         */

void time_tick_start(uint32_t hz)
{
    if (hz == 0) hz = 1000;

    s_ms_base    = time_now_ms();     /* freeze the current ms before hz changes */
    s_ticks_base = s_ticks;
    s_tick_hz    = hz;

    /* SysTick_Config (core_riscv.h): CMP = ticks-1, STCLK=HCLK, STRE (reload),
       STIE + STE, and PFIC_EnableIRQ(SysTick_IRQn). Reloads the hardware VAL;
       our software s_ticks is untouched. */
    SysTick->SR = 0;
    SysTick_Config(GetSysClock() / hz);
}

uint32_t time_ticks(void)
{
    return s_ticks;
}

uint32_t time_now_ms(void)
{
    /* 64-bit intermediate: safe until s_ticks - s_ticks_base exceeds ~2^32. */
    return s_ms_base +
           (uint32_t)(((uint64_t)(s_ticks - s_ticks_base) * 1000u) / s_tick_hz);
}

__INTERRUPT
__HIGH_CODE
void SysTick_Handler(void)
{
    SysTick->SR = 0;            /* clear CNTIF */
    s_ticks++;
}
