/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/gpio.h>

#include "hooks.h"

static void board_suspend(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_5v_usm), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_suspend, HOOK_PRIO_DEFAULT);

static void board_resume(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_5v_usm), 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_resume, HOOK_PRIO_DEFAULT);
