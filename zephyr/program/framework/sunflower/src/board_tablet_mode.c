/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "tablet_mode.h"

static void board_tablet_mode_change(void)
{
	/*
	 * gpio_ec_pad_mode is connected to the SoC and in ACPI there's a
	 * device PNP0C60/INT33D3 with GPIO config to read it.
	 * OS is notified of changes and state of this GPIO.
	 *
	 * Set this gpio to low if changing to notebook (clamshell) mode.
	 * Set this gpio to high if changing to tablet mode.
	 *
	 * At EC boot it's in laptop mode by GPIO configuration.
	 */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pad_mode),
			tablet_get_mode());

  /* Disable touchpad in tablet mode */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tp_en),
			tablet_get_mode());
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, board_tablet_mode_change, HOOK_PRIO_DEFAULT);
/* Run after gmr_tablet_switch_init to initialize GPIO after debounce. */
DECLARE_HOOK(HOOK_INIT, board_tablet_mode_change, HOOK_PRIO_DEFAULT + 1);
