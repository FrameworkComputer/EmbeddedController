/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "tablet_mode.h"

static void board_nb_mode_change(void)
{
	/*
	 * gpio_soc_ec_ish_nb_mode_l is an active low pin; default level is
	 * low. This pin is an output from SOC (ISH) to EC.
	 *
	 * In this config, ISH runs motion sense task; while EC doesn't.
	 * When ISH motion sense task detects notebook(clamshell)/tablet mode
	 * changes, ISH will notify EC about the change by updating this pin.
	 *
	 * Set this gpio to low if changing to notebook(clamshell) mode;
	 * Set this gpio to high if changing to tablet mode.
	 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_soc_ec_ish_nb_mode_l),
			tablet_get_mode());
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, board_nb_mode_change, HOOK_PRIO_DEFAULT);
