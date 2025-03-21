/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cros_cbi.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(trulo, LOG_LEVEL_INF);

static void bkoff_on_deferred(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bl_en_od), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, bkoff_on_deferred, HOOK_PRIO_DEFAULT);

static void bkon_off_deferred(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bl_en_od), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, bkon_off_deferred, HOOK_PRIO_DEFAULT);
