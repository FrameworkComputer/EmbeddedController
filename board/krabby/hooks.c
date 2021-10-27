/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "hooks.h"

static void board_suspend(void)
{
	gpio_set_level(GPIO_EN_5V_USM, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_suspend, HOOK_PRIO_DEFAULT);

static void board_resume(void)
{
	gpio_set_level(GPIO_EN_5V_USM, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_resume, HOOK_PRIO_DEFAULT);
