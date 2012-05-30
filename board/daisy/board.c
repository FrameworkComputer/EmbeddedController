/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Daisy board-specific configuration */

#include "board.h"
#include "common.h"
#include "dma.h"
#include "gpio.h"
#include "registers.h"
#include "spi.h"
#include "util.h"

/*
 * Daisy keyboard summary:
 * 1. KEYSCAN task woken up via GPIO external interrupt when a key is pressed.
 * 2. The task scans the keyboard matrix for changes. If key state has
 *    changed, the board-specific kb_send() function is called.
 * 3. For Daisy, the EC is connected via I2C and acts as a slave, so the AP
 *    must initiate all transactions. EC_INT is driven low to interrupt AP when
 *    new data becomes available.
 * 4. When the AP is interrupted, it initiates two i2c transactions:
 *    1. 1-byte write: AP writes 0x01 to make EC send keyboard state.
 *    2. 14-byte read: AP reads 1 keyboard packet (13 byte keyboard state +
 *       1-byte checksum).
 */

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT (GPIO_OUTPUT | GPIO_PULL_UP | GPIO_OPEN_DRAIN)

/* GPIO interrupt handlers prototypes */
#ifndef CONFIG_TASK_GAIAPOWER
#define gaia_power_event NULL
#else
void gaia_power_event(enum gpio_signal signal);
#endif
#ifndef CONFIG_TASK_KEYSCAN
#define matrix_interrupt NULL
#endif

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"KB_PWR_ON_L", GPIO_B, (1<<5),  GPIO_INT_BOTH, gaia_power_event},
	{"PP1800_LDO2", GPIO_A, (1<<1),  GPIO_INT_BOTH, gaia_power_event},
	{"XPSHOLD",     GPIO_A, (1<<3),  GPIO_INT_RISING, gaia_power_event},
	{"CHARGER_INT", GPIO_C, (1<<4),  GPIO_INT_RISING, NULL},
	{"LID_OPEN",    GPIO_C, (1<<13), GPIO_INT_BOTH, NULL},
	{"KB_IN00",     GPIO_C, (1<<8),  GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN01",     GPIO_C, (1<<9),  GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN02",     GPIO_C, (1<<10), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN03",     GPIO_C, (1<<11), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN04",     GPIO_C, (1<<12), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN05",     GPIO_C, (1<<14), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN06",     GPIO_C, (1<<15), GPIO_KB_INPUT, matrix_interrupt},
	{"KB_IN07",     GPIO_D, (1<<2),  GPIO_KB_INPUT, matrix_interrupt},
	/* Other inputs */
	{"SPI1_NSS",    GPIO_A, (1<<4), GPIO_INT_RISING, NULL},

	/* Outputs */
	{"EN_PP1350",   GPIO_A, (1<<2),  GPIO_OUT_LOW, NULL},
	{"EN_PP5000",   GPIO_A, (1<<11),  GPIO_OUT_LOW, NULL},
	{"EN_PP3300",   GPIO_A, (1<<8),  GPIO_OUT_LOW, NULL},
	{"PMIC_PWRON_L", GPIO_A, (1<<12), GPIO_OUT_HIGH, NULL},
	{"ENTERING_RW", GPIO_H, (1<<0),  GPIO_OUT_LOW, NULL},
	{"CHARGER_EN",  GPIO_B, (1<<2),  GPIO_OUT_LOW, NULL},
	{"EC_INT",      GPIO_B, (1<<9),  GPIO_HI_Z, NULL},
	{"CODEC_INT",   GPIO_H, (1<<1),  GPIO_HI_Z, NULL},
	{"KB_OUT00",    GPIO_B, (1<<0),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT01",    GPIO_B, (1<<8),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT02",    GPIO_B, (1<<12), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT03",    GPIO_B, (1<<13), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT04",    GPIO_B, (1<<14), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT05",    GPIO_B, (1<<15), GPIO_KB_OUTPUT, NULL},
	{"KB_OUT06",    GPIO_C, (1<<0),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT07",    GPIO_C, (1<<1),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT08",    GPIO_C, (1<<2),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT09",    GPIO_B, (1<<1),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT10",    GPIO_C, (1<<5),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT11",    GPIO_C, (1<<6),  GPIO_KB_OUTPUT, NULL},
	{"KB_OUT12",    GPIO_C, (1<<7),  GPIO_KB_OUTPUT, NULL},
};

void configure_board(void)
{
	dma_init();

	/* Enable all GPIOs clocks
	 * TODO: more fine-grained enabling for power saving
	 */
	STM32_RCC_AHBENR |= 0x3f;
	/* Required to configure external IRQ lines (SYSCFG_EXTICRn) */
	/* FIXME: This seems to break USB download in U-Boot (?!?) */
	STM32_RCC_APB2ENR |= 1 << 0;

	/* Enable SPI */
	STM32_RCC_APB2ENR |= (1<<12);

	/* SPI1 on pins PA4-7 (push-pull, no pullup/down, 10MHz) */
	STM32_GPIO_PUPDR_OFF(GPIO_A) &= ~((2 << (7 * 2)) |
					(2 << (6 * 2)) |
					(2 << (5 * 2)) |
					(2 << (4 * 2)));
	STM32_GPIO_OTYPER_OFF(GPIO_A) &= ~((1 << 7) |
					(1 << 6) |
					(1 << 5) |
					(1 << 4));
	gpio_set_alternate_function(GPIO_A, (1<<7) |
					(1<<6) |
					(1<<5) |
					(1<<4), GPIO_ALT_SPI);
	STM32_GPIO_OSPEEDR_OFF(GPIO_A) |= 0xff00;

	/*
	 * I2C SCL/SDA on PB10-11, bi-directional, no pull-up/down, initialized
	 * as hi-Z until alt. function is set
	 */
	STM32_GPIO_PUPDR_OFF(GPIO_B) &= ~((3 << (11*2)) | (3 << (10*2)));
	STM32_GPIO_MODER_OFF(GPIO_B) &= ~((3 << (11*2)) | (3 << (10*2)));
	STM32_GPIO_MODER_OFF(GPIO_B) |= (1 << (11*2)) | (1 << (10*2));
	STM32_GPIO_OTYPER_OFF(GPIO_B) |= (1<<11) | (1<<10);
	STM32_GPIO_BSRR_OFF(GPIO_B) |= (1<<11) | (1<<10);
	gpio_set_alternate_function(GPIO_B, (1<<11) | (1<<10), GPIO_ALT_I2C);

	/* Select Alternate function for USART1 on pins PA9/PA10 */
	gpio_set_alternate_function(GPIO_A, (1<<9) | (1<<10), GPIO_ALT_USART);

	/* EC_INT is output, open-drain */
	STM32_GPIO_OTYPER_OFF(GPIO_B) |= (1<<9);
	STM32_GPIO_PUPDR_OFF(GPIO_B) &= ~(0x3 << (2*9));
	STM32_GPIO_MODER_OFF(GPIO_B) &= ~(0x3 << (2*9));
	STM32_GPIO_MODER_OFF(GPIO_B) |= 0x1 << (2*9);
	/* put GPIO in Hi-Z state */
	gpio_set_level(GPIO_EC_INT, 1);
}

void board_interrupt_host(int active)
{
	/* interrupt host by using active low EC_INT signal */
	gpio_set_level(GPIO_EC_INT, !active);
}

void board_keyboard_suppress_noise(void)
{
	/* notify audio codec of keypress for noise suppression */
	gpio_set_level(GPIO_CODEC_INT, 0);
	gpio_set_level(GPIO_CODEC_INT, 1);
}
