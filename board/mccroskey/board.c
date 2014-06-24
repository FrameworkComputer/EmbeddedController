/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* McCroskey board-specific configuration */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define GPIO_KB_INPUT  (GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_BOTH)
#define GPIO_KB_OUTPUT (GPIO_OUTPUT | GPIO_OPEN_DRAIN | GPIO_OUT_LOW)

#define HARD_RESET_TIMEOUT_MS 5

#include "gpio_list.h"

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	/*
	 * TODO(crosbug.com/p/23494): Use this instead of hard-coded register
	 * writes in board_config_pre_init().
	 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

void board_config_pre_init(void)
{
	uint32_t val;

	/* Enable all GPIOs clocks.
	 *
	 * TODO(crosbug.com/p/23494): more fine-grained enabling for power
	 * saving.
	 */
	STM32_RCC_APB2ENR |= 0x1fd;

#ifdef CONFIG_SPI
	/*
	 * SPI1 on pins PA4-7 (alt. function push-pull, 10MHz).
	 *
	 * TODO(crosbug.com/p/23494): Connected device SPI freq is fxo/2 in
	 * master mode, fxo/4 in slave mode. fxo ranges from 12-40MHz.
	 */
	val = STM32_GPIO_CRL(GPIO_A) & ~0xffff0000;
	val |= 0x99990000;
	STM32_GPIO_CRL(GPIO_A) = val;
#endif

	/* remap OSC_IN/OSC_OUT to PD0/PD1 */
	STM32_GPIO_AFIO_MAPR |= 1 << 15;

	/* use PB3 as a GPIO, so disable JTAG and keep only SWD */
	STM32_GPIO_AFIO_MAPR = (STM32_GPIO_AFIO_MAPR & ~(0x7 << 24))
			       | (2 << 24);

	/* remap TIM2_CH2 to PB3 */
	STM32_GPIO_AFIO_MAPR = (STM32_GPIO_AFIO_MAPR & ~(0x3 << 8))
			       | (1 << 8);

	/*
	 * Set alternate function for USART1. For alt. function input
	 * the port is configured in either floating or pull-up/down
	 * input mode (ref. section 7.1.4 in datasheet RM0041):
	 * PA9:  Tx, alt. function output
	 * PA10: Rx, input with pull-down
	 *
	 * note: see crosbug.com/p/12223 for more info
	 */
	val = STM32_GPIO_CRH(GPIO_A) & ~0x00000ff0;
	val |= 0x00000890;
	STM32_GPIO_CRH(GPIO_A) = val;
}

/* GPIO configuration to be done after I2C module init */
void board_i2c_post_init(int port)
{
	uint32_t val;

	/* enable alt. function (open-drain) */
	if (port == STM32_I2C1_PORT) {
		/* I2C1 is on PB6-7 */
		val = STM32_GPIO_CRL(GPIO_B) & ~0xff000000;
		val |= 0xdd000000;
		STM32_GPIO_CRL(GPIO_B) = val;
	}
}
