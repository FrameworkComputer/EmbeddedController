/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Kukui SCP configuration */

#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Build GPIO tables */
void eint_event(enum gpio_signal signal);

#include "gpio_list.h"


void eint_event(enum gpio_signal signal)
{
	ccprintf("EINT event: %d\n", signal);
}

/* Initialize board.  */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_EINT5_TP);
	gpio_enable_interrupt(GPIO_EINT6_TP);
	gpio_enable_interrupt(GPIO_EINT7_TP);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
