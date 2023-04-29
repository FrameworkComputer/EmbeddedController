/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "cros_board_info.h"
#include "hooks.h"

#include <zephyr/drivers/gpio.h>

static void usb_porta_startup(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usbhub_en), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, usb_porta_startup, HOOK_PRIO_DEFAULT);

static void usb_porta_shutdown(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usbhub_en), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usb_porta_shutdown, HOOK_PRIO_DEFAULT);
