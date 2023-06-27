/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "console.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

void ac_feedback_lcd(void)
{
	if (extpower_is_present()) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ac_lcd), 1);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ac_lcd), 0);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, ac_feedback_lcd, HOOK_PRIO_DEFAULT);
