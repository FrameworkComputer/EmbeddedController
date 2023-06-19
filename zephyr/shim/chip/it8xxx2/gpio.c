/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio/gpio.h"
#include "gpio_it8xxx2.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

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

int gpio_configure_port_pin(int port, int id, int flags)
{
	const struct device *dev;

	/*
	 * Port number mapping to node
	 * 0                      gpioa
	 * ...                    ...
	 * 50                     gpioksi
	 * 51                     gpioksoh
	 * 52                     gpioksol
	 */
	switch ((enum gpio_port_to_node)port) {
	case GPIO_A:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpioa));
		break;
	case GPIO_B:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpiob));
		break;
	case GPIO_C:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpioc));
		break;
	case GPIO_D:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpiod));
		break;
	case GPIO_E:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpioe));
		break;
	case GPIO_F:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpiof));
		break;
	case GPIO_G:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpiog));
		break;
	case GPIO_H:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpioh));
		break;
	case GPIO_I:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpioi));
		break;
	case GPIO_J:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpioj));
		break;
	case GPIO_K:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpiok));
		break;
	case GPIO_L:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpiol));
		break;
	case GPIO_M:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpiom));
		break;
	case GPIO_KSI:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpioksi));
		break;
	case GPIO_KSOH:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpioksoh));
		break;
	case GPIO_KSOL:
		dev = DEVICE_DT_GET(DT_NODELABEL(gpioksol));
		break;
	default:
		printk("Error port number %d\n", port);
		return -EINVAL;
	}

	return gpio_pin_configure(dev, id, flags);
}
