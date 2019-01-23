/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "debug_printf.h"
#include "link_defs.h"
#include "registers.h"
#include "setup.h"

/* Is there a system wide function for this? */
void halt(void)
{
	while (1)
		;
}

void checkBuildVersion(void)
{
	uint32_t last_sync = GREG32(SWDP, P4_LAST_SYNC);

	if (last_sync == GC_SWDP_P4_LAST_SYNC_DEFAULT)
		return;

	debug_printf("compiled for %u, not willing to run on %u\n",
		     GC_SWDP_P4_LAST_SYNC_DEFAULT, last_sync);
	halt();
}

void unlockFlashForRW(void)
{
	uint32_t text_end = ((uint32_t)(&__data_lma_start) +
			     (uint32_t)(&__data_end) -
			     (uint32_t)(&__data_start) +
			     CONFIG_FLASH_BANK_SIZE)
		& ~(CONFIG_FLASH_BANK_SIZE - 1);

	GREG32(GLOBALSEC, FLASH_REGION1_BASE_ADDR) = text_end;
	GREG32(GLOBALSEC, FLASH_REGION1_SIZE) =
		CONFIG_FLASH_SIZE - text_end - 1;
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION1_CTRL, EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION1_CTRL, RD_EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION1_CTRL, WR_EN, 0);
}

void disarmRAMGuards(void)
{
	GWRITE_FIELD(GLOBALSEC, CPU0_D_REGION0_CTRL, EN, 1);
	GWRITE_FIELD(GLOBALSEC, CPU0_D_REGION0_CTRL, RD_EN, 1);
	GWRITE_FIELD(GLOBALSEC, CPU0_D_REGION0_CTRL, WR_EN, 1);
	GWRITE_FIELD(GLOBALSEC, CPU0_D_REGION1_CTRL, EN, 1);
	GWRITE_FIELD(GLOBALSEC, CPU0_D_REGION1_CTRL, RD_EN, 1);
	GWRITE_FIELD(GLOBALSEC, CPU0_D_REGION1_CTRL, WR_EN, 1);
}
