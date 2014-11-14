/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "task.h"
#include "util.h"

#include "gpio_list.h"

/* Initialize board. */
static void board_init(void)
{
	/* TODO(crosbug.com/p/33812): Try enabling this */
	/* gpio_enable_interrupt(GPIO_CAMO0_BREACH_INT); */
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
