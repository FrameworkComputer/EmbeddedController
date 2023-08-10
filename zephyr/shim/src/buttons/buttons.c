/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "power_button.h"

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/buttons.h>

LOG_MODULE_REGISTER(button, CONFIG_GPIO_LOG_LEVEL);

#define DT_DRV_COMPAT gpio_keys

static void buttons_cb_handler(struct input_event *evt)
{
	if (evt->type != INPUT_EV_KEY) {
		return;
	}

	LOG_DBG("Button %s, code=%u, pin_state=%d", evt->dev->name, evt->code,
		evt->value);

	switch (evt->code) {
	case BUTTON_POWER:
		handle_power_button(evt->value);
		break;
	default:
		LOG_ERR("Unknown button code=%u", evt->code);
		break;
	}
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET_ONE(gpio_keys), buttons_cb_handler);
