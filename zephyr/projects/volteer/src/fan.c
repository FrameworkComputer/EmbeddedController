/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Temporary fan stub for Zephyr running on Volteer.
 * TODO: b/177854276, b/174851465
 * Remove once temperature sensor and a Zephyr fan API are available.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "pwm.h"

static void board_fan_chipset_startup(void)
{
	gpio_set_level(GPIO_EN_PP5000_FAN, 1);
	pwm_enable(PWM_CH_FAN, 1);
	pwm_set_duty(PWM_CH_FAN, 50);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_fan_chipset_startup,
	     HOOK_PRIO_DEFAULT);

static void board_fan_chipset_shutdown(void)
{
	gpio_set_level(GPIO_EN_PP5000_FAN, 0);
	pwm_enable(PWM_CH_FAN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_fan_chipset_shutdown,
	     HOOK_PRIO_DEFAULT);
