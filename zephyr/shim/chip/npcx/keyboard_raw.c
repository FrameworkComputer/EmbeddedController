/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions needed by keyboard scanner module for Chrome EC */

#include <device.h>
#include <drivers/gpio.h>
#include <logging/log.h>
#include <soc.h>
#include <soc_gpio.h>
#include <zephyr.h>

#include "drivers/cros_kb_raw.h"
#include "keyboard_raw.h"

/**
 * Return true if the current value of the given input GPIO port is zero
 */
int keyboard_raw_is_input_low(int port, int id)
{
	const struct device *io_dev = npcx_get_gpio_dev(port);

	return gpio_pin_get_raw(io_dev, id) == 0;
}
