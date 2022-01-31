/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>

#include "cbi.h"
#include "gpio.h"
#include "hooks.h"

/* Enable/Disable keyboard backlight gpio */
static inline void kbd_backlight_enable(bool enable)
{
	if (get_board_id() == 1)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_id_1_ec_kb_bl_en),
				enable);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_kb_bl_en_l),
				!enable);
}

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* Allow keyboard backlight to be enabled */

	kbd_backlight_enable(true);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/* Turn off the keyboard backlight if it's on. */

	kbd_backlight_enable(false);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/*
 * Explicitly apply the board ID 1 *gpio.inc settings to pins that
 * were reassigned on current boards.
 */

static void set_board_id_1_gpios(void)
{
	if (get_board_id() != 1)
		return;

	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_id_1_ec_kb_bl_en),
			      GPIO_OUT_LOW);
}
DECLARE_HOOK(HOOK_INIT, set_board_id_1_gpios, HOOK_PRIO_FIRST);
