/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <atomic.h>
#include <zephyr/init.h>
#include "gpio/gpio_int.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "task.h"
#include "util.h"
#include "zephyr_console_shim.h"
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(inputmodule, LOG_LEVEL_ERR);

int oc_count;

void module_oc_interrupt(enum gpio_signal signal)
{
    oc_count++;
}



/* EC console command */

static int inputdeck_status(int argc, const char **argv)
{
    ccprintf("Input module Overcurrent Events: %d\n", oc_count);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(inputdeck, inputdeck_status, "",
			"Get Input modules status");