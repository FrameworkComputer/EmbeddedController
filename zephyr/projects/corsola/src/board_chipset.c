/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola baseboard-chipset specific configuration */

#include <drivers/gpio.h>

#include "common.h"
#include "hooks.h"

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);
