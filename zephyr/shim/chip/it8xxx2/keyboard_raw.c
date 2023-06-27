/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions needed by keyboard scanner module for Chrome EC */

#include "drivers/cros_kb_raw.h"
#include "gpio_it8xxx2.h"
#include "keyboard_raw.h"

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <soc.h>

/**
 * Return true if the current value of the given gpioksi/gpioksoh/gpioksol
 * port is zero
 */
int keyboard_raw_is_input_low(int port, int id)
{
	const struct device *dev;

	switch ((enum gpio_port_to_node)port) {
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
		printk("Error port number %d, return 0\n", port);
		return 0;
	}

	return (gpio_pin_get_raw(dev, id) == 0);
}
