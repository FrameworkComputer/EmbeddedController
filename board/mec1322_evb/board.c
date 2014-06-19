/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* MEC1322 eval board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "fan.h"
#include "gpio.h"
#include "i2c.h"
#include "registers.h"
#include "util.h"

#define GPIO_KB_INPUT GPIO_INPUT
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH | GPIO_PULL_UP)

#include "gpio_list.h"

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_PORT(16), 0x24,     1, MODULE_UART}, /* UART0 */
	{GPIO_PORT(3),  (1 << 4), 3, MODULE_PWM_FAN},
	{GPIO_PORT(14), (1 << 0), 3, MODULE_PWM_FAN},
	/*
	 * I2C0: External pull-up resistors on EVB are too weak. Let's
	 * also enable internal pull-up here.
	 */
	{GPIO_PORT(1),  0x60,     2, MODULE_I2C, GPIO_PULL_UP},
	{GPIO_PORT(0),  0xfe,     3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT},
	{GPIO_PORT(1),  0x03,     3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT},
	{GPIO_PORT(3),  0x04,     3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT},
	{GPIO_PORT(4),  0x0d,     3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT},
	{GPIO_PORT(10), 0xd8,     3, MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT},
	{GPIO_PORT(12), 0x60,     2, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT},
	{GPIO_PORT(14), 0x14,     3, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT},
	{GPIO_PORT(2),  0x20,     2, MODULE_LPC},
	{GPIO_PORT(12), 0x14,     1, MODULE_SPI},
	{GPIO_PORT(6),  0x10,     1, MODULE_SPI},
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_CH_1] = {"ADC1", 1, 1, 0, MEC1322_ADC_CH(1)},
	[ADC_CH_2] = {"ADC2", 1, 1, 0, MEC1322_ADC_CH(2)},
	[ADC_CH_3] = {"ADC3", 1, 1, 0, MEC1322_ADC_CH(3)},
	[ADC_CH_4] = {"ADC4", 1, 1, 0, MEC1322_ADC_CH(4)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_t fans[] = {
	{.flags = FAN_USE_RPM_MODE,
	 .rpm_min = 1500,
	 .rpm_max = 8000,
	 .ch = 0,
	 .pgood_gpio = -1,
	 .enable_gpio = -1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == CONFIG_FANS);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"port0", 0, 100},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
