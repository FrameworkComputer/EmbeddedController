/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer board-specific configuration */

#include "common.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

static void board_init(void)
{
	/* TODO */
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

