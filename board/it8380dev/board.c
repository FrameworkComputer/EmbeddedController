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
#include "pwm.h"
#include "pwm_chip.h"
#include "adc.h"
#include "adc_chip.h"

/* Test GPIO interrupt function that toggles one LED. */
void test_interrupt(enum gpio_signal signal)
{
	static int busy_state;

	/* toggle LED */
	busy_state = !busy_state;
	gpio_set_level(GPIO_BUSY_LED, busy_state);
}

#include "gpio_list.h"

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{0, 0},
	{1, PWM_CONFIG_ACTIVE_LOW},
	{2, 0},
	{3, PWM_CONFIG_ACTIVE_LOW},
	{4, 0},
	{5, PWM_CONFIG_ACTIVE_LOW},
	{6, 0},
	{7, PWM_CONFIG_ACTIVE_LOW},
};

BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Initialize board. */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_START_SW);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	/* Convert to mV (3000mV/1024). */
	{"adc_ch0", 3000, 1024, 0, 0},
	{"adc_ch1", 3000, 1024, 0, 1},
	{"adc_ch2", 3000, 1024, 0, 2},
	{"adc_ch3", 3000, 1024, 0, 3},
	{"adc_ch4", 3000, 1024, 0, 4},
	{"adc_ch5", 3000, 1024, 0, 5},
	{"adc_ch6", 3000, 1024, 0, 6},
	{"adc_ch7", 3000, 1024, 0, 7},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

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
