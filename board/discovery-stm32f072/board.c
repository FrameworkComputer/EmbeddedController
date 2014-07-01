/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* STM32F072-discovery board configuration */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "util.h"

void button_event(enum gpio_signal signal);

#include "gpio_list.h"

void button_event(enum gpio_signal signal)
{
	static int count = 0;

	gpio_set_level(GPIO_LED_U, (count & 0x03) == 0);
	gpio_set_level(GPIO_LED_R, (count & 0x03) == 1);
	gpio_set_level(GPIO_LED_D, (count & 0x03) == 2);
	gpio_set_level(GPIO_LED_L, (count & 0x03) == 3);

	count++;
}

/* Initialize board. */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_USER_BUTTON);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_A, 0xC000, 1, MODULE_UART}, /* USART2: PA14/PA15 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);
