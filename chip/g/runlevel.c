/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "init_chip.h"
#include "registers.h"

/* Drop run level to at least medium. */
void init_runlevel(const enum permission_level desired_level)
{
	volatile uint32_t *const reg_addrs[] = {
		/* CPU's use of the system peripheral bus */
		GREG32_ADDR(GLOBALSEC, CPU0_S_PERMISSION),
		/* CPU's use of the system bus via the debug access port */
		GREG32_ADDR(GLOBALSEC, CPU0_S_DAP_PERMISSION),
		/* DMA's use of the system peripheral bus */
		GREG32_ADDR(GLOBALSEC, DDMA0_PERMISSION),
		/*
		 * Current software level affects which (if any) scratch
		 * registers can be used for a warm boot hardware-verified
		 * jump.
		 */
		GREG32_ADDR(GLOBALSEC, SOFTWARE_LVL),
	};
	int i;

	/* Permission registers drop by 1 level (e.g. HIGHEST -> HIGH)
	 * each time a write is encountered (the value written does
	 * not matter).  So we repeat writes and reads, until the
	 * desired level is reached.
	 */
	for (i = 0; i < ARRAY_SIZE(reg_addrs); i++) {
		uint32_t current_level;

		while (1) {
			current_level = *reg_addrs[i];
			if (current_level <= desired_level)
				break;
			*reg_addrs[i] = desired_level;
		}
	}
}

int runlevel_is_high(void)
{
	return ((GREAD(GLOBALSEC, CPU0_S_PERMISSION) == PERMISSION_HIGH) ||
		(GREAD(GLOBALSEC, CPU0_S_PERMISSION) == PERMISSION_HIGHEST));
}
