/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "cros_board_info.h"
#include "hooks.h"

#include <zephyr/drivers/gpio.h>

static void check_usbhub_en(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
	    gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usbhub_en))) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usbhub_en), 0);
	} else if (!chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
		   !gpio_pin_get_dt(
			   GPIO_DT_FROM_NODELABEL(gpio_ec_usbhub_en))) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usbhub_en), 1);
	}
}
DECLARE_HOOK(HOOK_SECOND, check_usbhub_en, HOOK_PRIO_DEFAULT);
