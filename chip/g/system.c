/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cpu.h"
#include "registers.h"
#include "system.h"
#include "task.h"

static void check_reset_cause(void)
{
	uint32_t reset_source = GR_PMU_RSTSRC;
	uint32_t flags = 0;

	/* Clear the reset source now we have recorded it */
	GR_PMU_CLRRST = 1;

	if (reset_source & (1 << GC_PMU_RSTSRC_POR_LSB))
		flags |= RESET_FLAG_POWER_ON;
	else if (reset_source & (1 << GC_PMU_RSTSRC_RESETB_LSB))
		flags |= RESET_FLAG_RESET_PIN;
	else if (reset_source & (1 << GC_PMU_RSTSRC_EXIT_LSB))
		flags |= RESET_FLAG_WAKE_PIN;

	if (reset_source & (1 << GC_PMU_RSTSRC_WDOG_LSB))
		flags |= RESET_FLAG_WATCHDOG;

	if (reset_source & (1 << GC_PMU_RSTSRC_SOFTWARE_LSB))
		flags |= RESET_FLAG_HARD;
	if (reset_source & (1 << GC_PMU_RSTSRC_SYSRESET_LSB))
		flags |= RESET_FLAG_SOFT;

	if (reset_source & (1 << GC_PMU_RSTSRC_FST_BRNOUT_LSB))
		flags |= RESET_FLAG_BROWNOUT;

	if (reset_source && !flags)
		flags |= RESET_FLAG_OTHER;

	system_set_reset_flags(flags);
}

void system_pre_init(void)
{
	check_reset_cause();
}

void system_reset(int flags)
{
	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	if (flags & SYSTEM_RESET_HARD) /* Reset the full microcontroller */
		GR_PMU_GLOBAL_RESET = GC_PMU_GLOBAL_RESET_KEY;
	else /* Reset only the CPU core */
		CPU_NVIC_APINT = 0x05fa0004;

	/* Spin and wait for reboot; should never return  */
	while (1)
		;
}

const char *system_get_chip_vendor(void)
{
	return "g";
}

const char *system_get_chip_name(void)
{
	return "cr50";
}

const char *system_get_chip_revision(void)
{
	int build_date = GR_SWDP_BUILD_DATE;
	int build_time = GR_SWDP_BUILD_TIME;

	if ((build_date != GC_SWDP_BUILD_DATE_DEFAULT) ||
	    (build_time != GC_SWDP_BUILD_TIME_DEFAULT))
		return GC_REVISION_STR" BUILD MISMATCH!";
	else
		return GC_REVISION_STR;
}

int system_set_scratchpad(uint32_t value)
{
	GR_PMU_PWRDN_SCRATCH_HOLD_CLR = 1;
	GR_PMU_PWRDN_SCRATCH0 = value;
	GR_PMU_PWRDN_SCRATCH_HOLD_SET = 1;

	return EC_SUCCESS;
}

uint32_t system_get_scratchpad(void)
{
	return GR_PMU_PWRDN_SCRATCH0;
}

/* TODO(crosbug.com/p/33822): Where can we store stuff persistently? */
int system_get_vbnvcontext(uint8_t *block)
{
	return 0;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	return 0;
}
