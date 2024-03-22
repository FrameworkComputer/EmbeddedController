/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "fpsensor/fpsensor_detect.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/fff.h>
#include <zephyr/pm/policy.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(arm_core_mpu_disable);
FAKE_VOID_FUNC(LL_TIM_DisableCounter, void *);
FAKE_VALUE_FUNC(int, stm32_clock_control_off, const struct device *,
		clock_control_subsys_t);
FAKE_VALUE_FUNC(int, stm32_reset_line_toggle, const struct device *, uint32_t);

static struct clock_control_driver_api stm32_clock_control_api = {
	.off = stm32_clock_control_off,
};

DEVICE_DT_DEFINE(STM32_CLOCK_CONTROL_NODE, NULL, NULL, NULL, NULL, PRE_KERNEL_1,
		 0, &stm32_clock_control_api);

static struct reset_driver_api stm32_reset_api = {
	.line_toggle = stm32_reset_line_toggle,
};

DEVICE_DT_DEFINE(DT_NODELABEL(rctl), NULL, NULL, NULL, NULL, PRE_KERNEL_1, 0,
		 &stm32_reset_api);

static void *ec_quirks_setup(void)
{
	return NULL;
}

ZTEST_SUITE(ec_quirks, NULL, ec_quirks_setup, NULL, NULL, NULL);

ZTEST(ec_quirks, test_tim2)
{
	zassert_equal(LL_TIM_DisableCounter_fake.arg0_history[0],
		      (void *)DT_REG_ADDR(DT_NODELABEL(timers2)));
	zassert_equal(stm32_clock_control_off_fake.arg0_history[0],
		      DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE));
}
