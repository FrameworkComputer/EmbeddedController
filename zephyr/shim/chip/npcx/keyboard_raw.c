/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

/* Functions needed by keyboard scanner module for Chrome EC */

#include "drivers/cros_kb_raw.h"
#include "keyboard_raw.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <soc.h>
#include <soc_gpio.h>

/**
 * Return true if the current value of the given input GPIO port is zero
 */
int keyboard_raw_is_input_low(int port, int id)
{
	const struct device *io_dev = npcx_get_gpio_dev(port);

	return gpio_pin_get_raw(io_dev, id) == 0;
}
