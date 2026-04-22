/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "hooks.h"
#include "tablet_mode.h"
#include "lid_switch.h"
#include "gpio/gpio_int.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

static void board_touchpad_control(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_en),
			!tablet_get_mode() && lid_is_open());
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, board_touchpad_control, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_LID_CHANGE, board_touchpad_control, HOOK_PRIO_DEFAULT);
