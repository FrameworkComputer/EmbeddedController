/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* IT8380 development board configuration */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "util.h"

/* Test GPIO interrupt function that toggles one LED. */
void test_interrupt(enum gpio_signal signal)
{
	static int busy_state;

	/* toggle LED */
	busy_state = !busy_state;
	gpio_set_level(GPIO_BUSY_LED, busy_state);
}

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	{"H_LED0", GPIO_A, (1<<0), GPIO_ODR_HIGH},
	{"H_LED1", GPIO_A, (1<<1), GPIO_ODR_HIGH},
	{"H_LED2", GPIO_A, (1<<2), GPIO_ODR_HIGH},
	{"H_LED3", GPIO_A, (1<<3), GPIO_ODR_HIGH},
	{"H_LED4", GPIO_A, (1<<4), GPIO_ODR_HIGH},
	{"H_LED5", GPIO_A, (1<<5), GPIO_ODR_HIGH},
	{"H_LED6", GPIO_A, (1<<6), GPIO_ODR_HIGH},
	{"L_LED0", GPIO_I, (1<<0), GPIO_ODR_HIGH},
	{"L_LED1", GPIO_I, (1<<1), GPIO_ODR_HIGH},
	{"L_LED2", GPIO_I, (1<<2), GPIO_ODR_HIGH},
	{"L_LED3", GPIO_I, (1<<3), GPIO_ODR_HIGH},
	{"L_LED4", GPIO_I, (1<<4), GPIO_ODR_HIGH},
	{"L_LED5", GPIO_I, (1<<5), GPIO_ODR_HIGH},
	{"L_LED6", GPIO_I, (1<<6), GPIO_ODR_HIGH},
	{"BUSY_LED", GPIO_J, (1<<0), GPIO_OUT_LOW},
	{"GOOD_LED", GPIO_J, (1<<1), GPIO_OUT_HIGH},
	{"FAIL_LED", GPIO_J, (1<<2), GPIO_OUT_LOW},
	{"SW0", GPIO_E, (1<<0), GPIO_INPUT},
	{"SW1", GPIO_E, (1<<1), GPIO_INPUT | GPIO_PULL_DOWN},
	{"SW2", GPIO_E, (1<<2), GPIO_INPUT | GPIO_PULL_DOWN},
	{"SW3", GPIO_E, (1<<3), GPIO_INPUT | GPIO_PULL_DOWN},
	{"START_SW", GPIO_E, (1<<4), GPIO_INT_FALLING, test_interrupt},
	/* Unimplemented signals which we need to emulate for now */
	GPIO_SIGNAL_NOT_IMPLEMENTED("ENTERING_RW"),
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_B, 0x03, 1, MODULE_UART, GPIO_PULL_UP},	/* UART0 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

/* Initialize board. */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_START_SW);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console commands */

void display_7seg(uint8_t val)
{
	int i;
	static const uint8_t digits[16] = {
		0xc0, 0xf9, 0xa8, 0xb0,
		0x99, 0x92, 0x82, 0xf8,
		0x80, 0x98, 0x88, 0x83,
		0xc6, 0xa1, 0x86, 0x8e,
	};

	for (i = 0; i < 7; i++)
		gpio_set_level(GPIO_H_LED0 + i, digits[val >> 4] & (1 << i));
	for (i = 0; i < 7; i++)
		gpio_set_level(GPIO_L_LED0 + i, digits[val & 0xf] & (1 << i));
}

static int command_7seg(int argc, char **argv)
{
	uint8_t val;
	char *e;

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	val = strtoi(argv[1], &e, 16);
	if (*e)
		return EC_ERROR_PARAM1;

	ccprintf("display 0x%02x\n", val);
	display_7seg(val);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(seg7, command_7seg,
			"<hex>",
			"Print 8-bit value on 7-segment display",
			NULL);
