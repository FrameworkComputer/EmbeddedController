/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module ISH (Not implemented) */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "host_command.h"
#include "registers.h"
#include "shared_mem.h"
#include "system.h"
#include "hooks.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "spi.h"
#include "power_mgt.h"

/* Indices for hibernate data registers (RAM backed by VBAT) */
enum hibdata_index {
	HIBDATA_INDEX_SCRATCHPAD = 0,    /* General-purpose scratchpad */
	HIBDATA_INDEX_SAVED_RESET_FLAGS  /* Saved reset flags */
};

int system_is_reboot_warm(void)
{
	return 0;
}

void system_pre_init(void)
{
#ifdef CONFIG_LOW_POWER_IDLE
	ish_pm_init();
#endif
}

void chip_save_reset_flags(int flags)
{
}

uint32_t chip_read_reset_flags(void)
{
	return 0;
}

void _system_reset(int flags, int wake_from_hibernate)
{
#ifdef CONFIG_LOW_POWER_IDLE
	/**
	 * ish_pm_reset() do more (poweroff main SRAM etc) than
	 * ish_mia_reset() which just reset the ISH minute-ia cpu core
	 */
	ish_pm_reset();
#else
	ish_mia_reset();
#endif
}

void system_reset(int flags)
{
	_system_reset(flags, 0);
	while(1)
		;
}

const char *system_get_chip_vendor(void)
{
	return "intel";
}

const char *system_get_chip_name(void)
{
	return "intel";
}

static char to_hex(int x)
{
	if (x >= 0 && x <= 9)
		return '0' + x;
	return 'a' + x - 10;
}

const char *system_get_chip_revision(void)
{
	static char buf[3];
	uint8_t rev = 0x86;

	buf[0] = to_hex(rev / 16);
	buf[1] = to_hex(rev & 0xf);
	buf[2] = '\0';
	return buf;
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int system_set_bbram(enum system_bbram_idx idx, uint8_t value)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int system_set_scratchpad(uint32_t value)
{
	return EC_SUCCESS;
}

uint32_t system_get_scratchpad(void)
{
	return 0;
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
}

void htimer_interrupt(void)
{
	/* Time to wake up */
}

enum system_image_copy_t system_get_shrspi_image_copy(void)
{
	return 0;
}

uint32_t system_get_lfw_address(void)
{
	return 0;
}

void system_set_image_copy(enum system_image_copy_t copy)
{
}
