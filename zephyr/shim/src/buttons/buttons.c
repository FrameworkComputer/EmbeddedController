/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "power_button.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio_keys.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/buttons.h>

LOG_MODULE_REGISTER(button, CONFIG_GPIO_LOG_LEVEL);

#define DT_DRV_COMPAT zephyr_gpio_keys

static void buttons_cb_handler(const struct device *dev,
			       struct gpio_keys_callback *cbdata, uint32_t pins)
{
	LOG_DBG("Button %s, pins=0x%x, zephyr_code=%u, pin_state=%d", dev->name,
		pins, cbdata->zephyr_code, cbdata->pin_state);

	switch (cbdata->zephyr_code) {
	case BUTTON_POWER:
		handle_power_button(cbdata->pin_state);
		break;
	default:
		LOG_ERR("Unknown button code=%u", cbdata->zephyr_code);
		break;
	}
}

#define BUTTONS_ENABLE_INT(node_id)                                     \
	gpio_keys_enable_interrupt(DEVICE_DT_GET(DT_DRV_INST(node_id)), \
				   buttons_cb_handler)

static int buttons_init(const struct device *device)
{
	DT_INST_FOREACH_STATUS_OKAY(BUTTONS_ENABLE_INT);

	return 0;
}

SYS_INIT(buttons_init, POST_KERNEL, 51);
