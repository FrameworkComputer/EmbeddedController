/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Stellaris EKB-LM4F-EAC board-specific configuration */

#include "board.h"
#include "gpio.h"
#include "power_button.h"
#include "registers.h"
#include "util.h"


/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"POWER_BUTTONn", LM4_GPIO_C, (1<<5), GPIO_PULL_UP | GPIO_INT_BOTH,
	 power_button_interrupt},
	{"LID_SWITCHn",   LM4_GPIO_D, (1<<0), GPIO_PULL_UP | GPIO_INT_BOTH,
	 power_button_interrupt},
	/* Other inputs */
	/* Outputs */
	{"DEBUG_LED",    LM4_GPIO_A, (1<<7), GPIO_OUT_LOW, NULL},
	/* Unimplemented outputs which we need to emulate for now */
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_LID_SWITCHn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_PWRBTNn"),
};


void configure_board(void)
{
}
