/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cros_cbi.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "lid_switch.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(rex, CONFIG_REX_LOG_LEVEL);

/* touch panel power sequence control */

#define TOUCH_ENABLE_DELAY_MS (500 * MSEC)
#define TOUCH_DISABLE_DELAY_MS (0 * MSEC)

static bool touch_sequence_enable;

void touch_disable(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en), 0);
}
DECLARE_DEFERRED(touch_disable);

void touch_enable(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en), 1);
}
DECLARE_DEFERRED(touch_enable);

void soc_edp_bl_interrupt(enum gpio_signal signal)
{
	int state;

	if (signal != GPIO_SIGNAL(DT_NODELABEL(gpio_soc_3v3_edp_bl_en)))
		return;

	state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_soc_3v3_edp_bl_en));

	LOG_INF("%s: %d", __func__, state);

	if (state && lid_is_open()) {
		hook_call_deferred(&touch_enable_data, TOUCH_ENABLE_DELAY_MS);
	} else {
		hook_call_deferred(&touch_disable_data, TOUCH_DISABLE_DELAY_MS);
	}
}

static void touch_lid_change(void)
{
	if (!touch_sequence_enable)
		return;

	if (!lid_is_open()) {
		LOG_INF("%s: disable touch", __func__);
		hook_call_deferred(&touch_disable_data, TOUCH_DISABLE_DELAY_MS);
	} else {
		if (gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_soc_3v3_edp_bl_en)) &&
		    !gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en))) {
			LOG_INF("%s: enable touch", __func__);
			hook_call_deferred(&touch_enable_data,
					   TOUCH_ENABLE_DELAY_MS);
		}
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, touch_lid_change, HOOK_PRIO_DEFAULT);

static void touch_enable_init(void)
{
	int ret;
	uint32_t val;

	touch_sequence_enable = false;

	ret = cros_cbi_get_fw_config(FW_TOUCH_EN, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_TOUCH_EN);
		return;
	}

	LOG_INF("%s: %sable", __func__,
		(val == FW_TOUCH_EN_ENABLE) ? "en" : "dis");

	if (val != FW_TOUCH_EN_ENABLE)
		return;

	touch_sequence_enable = true;
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_soc_edp_bl_en));
}
DECLARE_HOOK(HOOK_INIT, touch_enable_init, HOOK_PRIO_DEFAULT);
