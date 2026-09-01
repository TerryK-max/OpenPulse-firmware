/******************************************************************************
 * board.h — CH570D board bring-up: clock, pin alt-functions.
 *****************************************************************************/
#ifndef BOARD_H
#define BOARD_H

/**
 * @brief  Bring the MCU up to its operating state.
 *         - disables the 2-wire debug interface (frees PA8/PA9 for I2C)
 *         - configures the HSE load capacitance
 *         - selects the 100 MHz HSE-PLL system clock (required for USB FS)
 *
 * Call once, first thing in main(), before any peripheral init.
 */
void board_init(void);

#endif /* BOARD_H */
