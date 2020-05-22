/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Honeybuns family-specific configuration */
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/******************************************************************************/
__overridable const struct power_seq board_power_seq[] = { };

__overridable const size_t board_power_seq_count =
	ARRAY_SIZE(board_power_seq);

static void board_power_sequence(void)
{
	int i;

	for(i = 0; i < board_power_seq_count; i++) {
		gpio_set_level(board_power_seq[i].signal,
			       board_power_seq[i].level);
		msleep(board_power_seq[i].delay_ms);
	}
}

static void baseboard_init(void)
{
	/* Turn on power rails */
	board_power_sequence();
	CPRINTS("board: Power rails enabled");
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_DEFAULT);
