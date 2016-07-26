/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "dma.h"
#include "ec_version.h"
#include "gpio.h"
#include "gpio_list.h"
#include "hooks.h"
#include "registers.h"
#include "stm32-dma.h"

#define GPIO_SET_HS(bank, number)	\
	(STM32_GPIO_OSPEEDR(GPIO_##bank) |= (0x3 << (number * 2)))

void board_config_post_gpio_init(void)
{
	/* We use MCO2 clock passthrough to provide a clock to USB HS */
	gpio_config_module(MODULE_MCO, 1);
	/* GPIO PC9 to high speed */
	GPIO_SET_HS(C, 9);

	/* Set USB GPIO to high speed */
	GPIO_SET_HS(A, 11);
	GPIO_SET_HS(A, 12);

	GPIO_SET_HS(C,  3);
	GPIO_SET_HS(C,  2);
	GPIO_SET_HS(C,  0);
	GPIO_SET_HS(A,  5);

	GPIO_SET_HS(B,  5);
	GPIO_SET_HS(B, 13);
	GPIO_SET_HS(B, 12);
	GPIO_SET_HS(B,  2);
	GPIO_SET_HS(B, 10);
	GPIO_SET_HS(B,  1);
	GPIO_SET_HS(B,  0);
	GPIO_SET_HS(A,  3);

	/* Set I2C GPIO to HS */
	GPIO_SET_HS(B,  6);
	GPIO_SET_HS(B,  7);
	GPIO_SET_HS(F,  1);
	GPIO_SET_HS(F,  0);
	GPIO_SET_HS(A,  8);
	GPIO_SET_HS(B,  4);
	GPIO_SET_HS(C,  6);
	GPIO_SET_HS(C,  7);
}

static void board_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

