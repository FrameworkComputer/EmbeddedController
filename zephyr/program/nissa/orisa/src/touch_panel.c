/* Copyright 2024 The ChromiumOS Authors
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
LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static void bkoff_on_deferred(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bl_en_od), 1);
}
DECLARE_DEFERRED(bkoff_on_deferred);

void soc_signal_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_soc_enbkl)))
		hook_call_deferred(&bkoff_on_deferred_data, 60 * MSEC);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bl_en_od),
				0);
}
