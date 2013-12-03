/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* MEC1322 eval board-specific configuration */

#include "fan.h"
#include "gpio.h"
#include "i2c.h"
#include "registers.h"
#include "util.h"

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	{"LED1", GPIO_PORT(15), (1 << 4), GPIO_ODR_LOW, NULL},
	{"LED2", GPIO_PORT(15), (1 << 5), GPIO_ODR_HIGH, NULL},
	{"LED3", GPIO_PORT(15), (1 << 6), GPIO_ODR_LOW, NULL},
	/* Unimplemented signals which we need to emulate for now */
	GPIO_SIGNAL_NOT_IMPLEMENTED("RECOVERYn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("WP"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("ENTERING_RW"),
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_PORT(16), 0x24,     1, MODULE_UART}, /* UART0 */
	{GPIO_PORT(3),  (1 << 4), 3, MODULE_PWM_FAN},
	{GPIO_PORT(14), (1 << 0), 3, MODULE_PWM_FAN},
	{GPIO_PORT(1),  0x60,     2, MODULE_I2C},  /* I2C0 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

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
