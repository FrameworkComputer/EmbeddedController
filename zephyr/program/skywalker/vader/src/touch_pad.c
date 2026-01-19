/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "chipset.h"
#include "cros_cbi.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "lid_switch.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

static void touchpad_enable_switch(void)
{
	if (lid_is_open() &&
	    (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON) ||
	     chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_SUSPEND)))
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchpad_lid_close),
				1);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tchpad_lid_close),
				0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, touchpad_enable_switch, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, touchpad_enable_switch, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_LID_CHANGE, touchpad_enable_switch, HOOK_PRIO_DEFAULT);
