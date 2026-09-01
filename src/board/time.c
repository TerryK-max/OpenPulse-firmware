#include "CH57x_common.h"
#include "core_riscv.h"
#include "board/time.h"

static volatile uint32_t s_ticks;
static uint32_t s_tick_hz = 1000;

void time_tick_start(uint32_t hz)
{
    if (hz == 0) hz = 1000;
    s_tick_hz = hz;
    s_ticks   = 0;

    /* SysTick_Config (core_riscv.h): CMP = ticks-1, STCLK=HCLK, STRE (reload),
       STIE + STE, and PFIC_EnableIRQ(SysTick_IRQn). */
    SysTick->SR = 0;
    SysTick_Config(GetSysClock() / hz);
}

uint32_t time_ticks(void)
{
    return s_ticks;
}

uint32_t time_now_ms(void)
{
    /* s_ticks * 1000 / hz, 64-bit to avoid overflow at high tick counts. */
    return (uint32_t)(((uint64_t)s_ticks * 1000u) / s_tick_hz);
}

__INTERRUPT
__HIGH_CODE
void SysTick_Handler(void)
{
    SysTick->SR = 0;            /* clear CNTIF */
    s_ticks++;
}
