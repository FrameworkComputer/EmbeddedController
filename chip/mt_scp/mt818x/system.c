/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System : hardware specific implementation */

#include "builtin/assert.h"
#include "clock_chip.h"
#include "console.h"
#include "cpu.h"
#include "cros_version.h"
#include "flash.h"
#include "hooks.h"
#include "host_command.h"
#include "memmap.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "watchdog.h"

/*
 * SCP_GPR[0] b15-b0  - scratchpad
 * SCP_GPR[0] b31-b16 - saved_flags
 */

int system_set_scratchpad(uint32_t value)
{
	/* Check if value fits in 16 bits */
	if (value & 0xffff0000)
		return EC_ERROR_INVAL;

	SCP_GPR[0] = (SCP_GPR[0] & 0xffff0000) | value;

	return EC_SUCCESS;
}

int system_get_scratchpad(uint32_t *value)
{
	*value = SCP_GPR[0] & 0xffff;
	return EC_SUCCESS;
}

const char *system_get_chip_vendor(void)
{
	return "mtk";
}

const char *system_get_chip_name(void)
{
	/* Support only SCP_A for now */
	return "scp_a";
}

const char *system_get_chip_revision(void)
{
	return "";
}

void chip_pre_init(void)
{
}

static void scp_cm4_mod(void)
{
	CM4_MODIFICATION = 3;
	CM4_DCM_FEATURE = 3;
}

static void scp_enable_pirq(void)
{
	/* Enable all peripheral to SCP IRQ, except IPC0. */
	SCP_INTC_IRQ_ENABLE = 0xFFFFFFFE;
	SCP_INTC_IRQ_ENABLE_MSB = 0xFFFFFFFF;
}

void system_pre_init(void)
{
	/* CM4 Modification */
	scp_cm4_mod();

	/* Clock */
	scp_enable_clock();

	/* Peripheral IRQ */
	scp_enable_pirq();
	/* Init dram mapping (and cache) */
	scp_memmap_init();
	/* Disable jump (mt_scp has only RW) and enable MPU. */
	system_disable_jump();
}

void system_reset(int flags)
{
	uint32_t save_flags = 0;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | EC_RESET_FLAG_PRESERVED;

	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= EC_RESET_FLAG_AP_OFF;

	/* Remember that the software asked us to hard reboot */
	if (flags & SYSTEM_RESET_HARD)
		save_flags |= EC_RESET_FLAG_HARD;

	/* Reset flags are 32-bits, but save only 16 bits. */
	ASSERT(!(save_flags >> 16));
	SCP_GPR[0] = (save_flags << 16) | (SCP_GPR[0] & 0xffff);

	/* SCP can not hard reset itself */
	ASSERT(!(flags & SYSTEM_RESET_HARD));

	if (flags & SYSTEM_RESET_WAIT_EXT) {
		int i;

		/* Wait 10 seconds for external reset */
		for (i = 0; i < 1000; i++) {
			watchdog_reload();
			udelay(10000);
		}
	}

	/* Set watchdog timer to small value, and spin wait for watchdog reset
	 */
	SCP_WDT_CFG = 0;
	SCP_WDT_CFG = SCP_WDT_ENABLE | SCP_WDT_PERIOD(1);
	watchdog_reload();
	while (1)
		;
}

static void check_reset_cause(void)
{
	__maybe_unused uint32_t flags = 0;
	uint32_t raw_reset_cause = SCP_GPR[1];

	/* Set state to power-on */
	SCP_PWRON_STATE = PWRON_DEFAULT;

	if ((raw_reset_cause & 0xffff0000) == PWRON_DEFAULT) {
		/* Reboot */
		if (raw_reset_cause & PWRON_WATCHDOG)
			flags |= EC_RESET_FLAG_WATCHDOG;
		else if (raw_reset_cause & PWRON_RESET)
			flags |= EC_RESET_FLAG_POWER_ON;
		else
			flags |= EC_RESET_FLAG_OTHER;
	} else {
		/* Power lost restart */
		flags |= EC_RESET_FLAG_POWER_ON;
	}
	system_set_reset_flags(SCP_GPR[0] >> 16);
	SCP_GPR[0] &= 0xffff;
}

int system_is_reboot_warm(void)
{
	const uint32_t cold_flags =
		EC_RESET_FLAG_RESET_PIN | EC_RESET_FLAG_POWER_ON |
		EC_RESET_FLAG_WATCHDOG | EC_RESET_FLAG_HARD |
		EC_RESET_FLAG_SOFT | EC_RESET_FLAG_HIBERNATE;

	check_reset_cause();

	return !(system_get_reset_flags() & cold_flags);
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	return EC_ERROR_INVAL;
}
