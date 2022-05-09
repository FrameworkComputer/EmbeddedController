/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "gpio/gpio.h"

LOG_MODULE_REGISTER(shim_cros_gpio, LOG_LEVEL_ERR);

static const struct unused_pin_config unused_pin_configs[] = {
	UNUSED_GPIO_CONFIG_LIST
};

int gpio_config_unused_pins(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(unused_pin_configs); ++i) {
		int rv;
		int flags;
		const struct device *dev =
			device_get_binding(unused_pin_configs[i].dev_name);

		if (dev == NULL) {
			LOG_ERR("Not found (%s)",
				unused_pin_configs[i].dev_name);
			return -ENOTSUP;
		}

		/*
		 * Set the default setting for the floating IOs. The floating
		 * IOs cause the leakage current. Set unused pins as input with
		 * internal PU to prevent extra power consumption.
		 */
		if (unused_pin_configs[i].flags == 0)
			flags = GPIO_INPUT | GPIO_PULL_UP;
		else
			flags = unused_pin_configs[i].flags;

		rv = gpio_pin_configure(dev, unused_pin_configs[i].pin, flags);

		if (rv < 0) {
			LOG_ERR("Config failed %s-%d (%d)",
				unused_pin_configs[i].dev_name,
				unused_pin_configs[i].pin, rv);
			return rv;
		}
	}

	return 0;
}
