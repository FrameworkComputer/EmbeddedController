/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "gpio.h"
#include "hooks.h"

#include <zephyr/drivers/gpio.h>

static void fp_led_suspend(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_fp_rxd1), false);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, fp_led_suspend, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, fp_led_suspend, HOOK_PRIO_DEFAULT);

static void fp_led_resume(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_fp_rxd1), true);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, fp_led_resume, HOOK_PRIO_DEFAULT);
