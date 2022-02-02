/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "hooks.h"
#include "power.h"
#include "task.h"

void board_chipset_pre_init(void)
{
	/* Turn on the 3.3V rail */
	gpio_set_level(GPIO_EN_PP3300_A, 1);

	/* Turn on the 5V rail. */
#ifdef CONFIG_POWER_PP5000_CONTROL
	power_5v_enable(task_get_current(), 1);
#else /* !defined(CONFIG_POWER_PP5000_CONTROL) */
	gpio_set_level(GPIO_EN_PP5000, 1);
#endif /* defined(CONFIG_POWER_PP5000_CONTROL) */
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_chipset_pre_init, HOOK_PRIO_DEFAULT);

void board_chipset_shutdown_complete(void)
{
	/* Turn off the 5V rail. */
#ifdef CONFIG_POWER_PP5000_CONTROL
	power_5v_enable(task_get_current(), 0);
#else /* !defined(CONFIG_POWER_PP5000_CONTROL) */
	gpio_set_level(GPIO_EN_PP5000, 0);
#endif /* defined(CONFIG_POWER_PP5000_CONTROL) */

	/* Turn off the 3.3V and 5V rails. */
	gpio_set_level(GPIO_EN_PP3300_A, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN_COMPLETE, board_chipset_shutdown_complete,
		HOOK_PRIO_DEFAULT);
