#include "CH57x_common.h"
#include "board/board.h"

void board_init(void)
{
    /* Disable the 2-wire debug interface (PA8/PA9) so those pins are free for
       I2C, and keep the USB D+/D- lines usable. */
    R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN;

    /* HSE crystal load capacitance (board-specific, 18 pF here). */
    HSECFG_Capacitance(HSECap_18p);

    /* 100 MHz from HSE PLL — required for reliable USB Full-Speed. */
    SetSysClock(CLK_SOURCE_HSE_PLL_100MHz);
}
