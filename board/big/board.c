/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Big board-specific configuration */

#include "battery.h"
#include "chipset.h"
#include "common.h"
#include "extpower.h"
#include "gpio.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "power.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "util.h"
#include "timer.h"
#include "charger.h"
#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_SOC1V8_XPSHOLD, 1, "XPSHOLD"},
	{GPIO_SUSPEND_L,      0, "SUSPEND#_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{STM32_TIM(2), STM32_TIM_CH(3), PWM_CONFIG_ACTIVE_LOW},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);
