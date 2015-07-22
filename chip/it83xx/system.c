/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : hardware specific implementation */

#include "console.h"
#include "cpu.h"
#include "flash.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "version.h"
#include "watchdog.h"

/* Battery backed RAM indices. */
enum system_bram_indices {
	BRAM_INDEX_SAVED_RESET_FLAGS = 0, /* uses 4 bytes */
};



void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
}

static void check_reset_cause(void)
{
	uint32_t flags = 0;
	uint8_t raw_reset_cause = IT83XX_GCTRL_RSTS & 0x03;
	uint8_t raw_reset_cause2 = IT83XX_GCTRL_SPCTRL4 & 0x07;

	/* Clear reset cause. */
	IT83XX_GCTRL_RSTS |= 0x03;
	IT83XX_GCTRL_SPCTRL4 |= 0x07;

	/* Determine if watchdog reset or power on reset. */
	if (raw_reset_cause & 0x02) {
		flags |= RESET_FLAG_WATCHDOG;
	} else if (raw_reset_cause & 0x01) {
		flags |= RESET_FLAG_POWER_ON;
	} else {
		if ((IT83XX_GCTRL_RSTS & 0xC0) == 0x80)
			flags |= RESET_FLAG_POWER_ON;
	}

	if (raw_reset_cause2 & 0x04)
		flags |= RESET_FLAG_POWER_ON;

	/* Restore then clear saved reset flags. */
	if (!(flags & RESET_FLAG_POWER_ON)) {
		const uint32_t addr =
			IT83XX_BRAM_BASE+BRAM_INDEX_SAVED_RESET_FLAGS;
		flags |= REG8(addr + 0) << 24;
		flags |= REG8(addr + 1) << 16;
		flags |= REG8(addr + 2) << 8;
		flags |= REG8(addr + 3);
	}

	REG8(IT83XX_BRAM_BASE+BRAM_INDEX_SAVED_RESET_FLAGS) = 0;
	REG8(IT83XX_BRAM_BASE+BRAM_INDEX_SAVED_RESET_FLAGS+1) = 0;
	REG8(IT83XX_BRAM_BASE+BRAM_INDEX_SAVED_RESET_FLAGS+2) = 0;
	REG8(IT83XX_BRAM_BASE+BRAM_INDEX_SAVED_RESET_FLAGS+3) = 0;

	system_set_reset_flags(flags);
}

int gpio_is_reboot_warm(void)
{
	uint32_t reset_flags;
	/*
	 * Check reset cause here,
	 * gpio_pre_init is executed faster than system_pre_init
	 */
	check_reset_cause();
	reset_flags = system_get_reset_flags();

	if ((reset_flags & RESET_FLAG_RESET_PIN) ||
	    (reset_flags & RESET_FLAG_POWER_ON) ||
	    (reset_flags & RESET_FLAG_WATCHDOG) ||
	    (reset_flags & RESET_FLAG_HARD) ||
	    (reset_flags & RESET_FLAG_SOFT))
		return 0;
	else
		return 1;
}

void system_pre_init(void)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */

}

void system_reset(int flags)
{
	uint32_t save_flags = 0;

	/* Disable interrupts to avoid task swaps during reboot. */
	interrupt_disable();

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | RESET_FLAG_PRESERVED;

	/* Add in AP off flag into saved flags. */
	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= RESET_FLAG_AP_OFF;

	/* Store flags to battery backed RAM. */
	REG8(IT83XX_BRAM_BASE+BRAM_INDEX_SAVED_RESET_FLAGS) = save_flags >> 24;
	REG8(IT83XX_BRAM_BASE+BRAM_INDEX_SAVED_RESET_FLAGS+1) =
			(save_flags >> 16) & 0xff;
	REG8(IT83XX_BRAM_BASE+BRAM_INDEX_SAVED_RESET_FLAGS+2) =
			(save_flags >> 8) & 0xff;
	REG8(IT83XX_BRAM_BASE+BRAM_INDEX_SAVED_RESET_FLAGS+3) =
			save_flags & 0xff;

	/*
	 * Writing invalid key to watchdog module triggers a soft reset. For
	 * now this is the only option, no hard reset.
	 */
	IT83XX_ETWD_ETWCFG |= 0x20;
	IT83XX_ETWD_EWDKEYR = 0x00;

	/* Spin and wait for reboot; should never return */
	while (1)
		;
}

int system_set_scratchpad(uint32_t value)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return 0;
}

uint32_t system_get_scratchpad(void)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return 0;
}

const char *system_get_chip_vendor(void)
{
	return "ite";
}

const char *system_get_chip_name(void)
{
	return "it83xx";
}

const char *system_get_chip_revision(void)
{
	return "";
}

int system_get_vbnvcontext(uint8_t *block)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return EC_SUCCESS;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return EC_SUCCESS;
}

int system_set_console_force_enabled(int val)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return 0;
}

int system_get_console_force_enabled(void)
{
	/* TODO(crosbug.com/p/23575): IMPLEMENT ME ! */
	return 0;
}

uintptr_t system_get_fw_reset_vector(uintptr_t base)
{
	uintptr_t reset_vector, num;

	num = *(uintptr_t *)base;
	reset_vector = ((num>>24)&0xff) | ((num<<8)&0xff0000) |
			((num>>8)&0xff00) | ((num<<24)&0xff000000);
	reset_vector = ((reset_vector & 0xffffff) << 1) + base;

	return reset_vector;
}
