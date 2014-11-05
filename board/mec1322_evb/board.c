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
	 .rpm_start = 1500,
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
