/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "ish_fwst.h"
#include "ish_persistent_data.h"
#include "power_mgt.h"
#include "registers.h"
#include "shared_mem.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

int system_is_reboot_warm(void)
{
	return !(system_get_reset_flags() &
		 (RESET_FLAG_POWER_ON | RESET_FLAG_HARD));
}

void system_pre_init(void)
{
	ish_fwst_set_fw_status(FWSTS_FW_IS_RUNNING);
	task_enable_irq(ISH_FABRIC_IRQ);
	ish_pm_init();
	ish_persistent_data_init();
}

void chip_save_reset_flags(uint32_t flags)
{
	ish_persistent_data.reset_flags = flags;
}

uint32_t chip_read_reset_flags(void)
{
	return ish_persistent_data.reset_flags;
}

void system_reset(int flags)
{
	uint32_t save_flags;

	/*
	 * We can't save any data when we do an ish_mia_reset(). Take
	 * the quick path out.
	 */
	if (!IS_ENABLED(CONFIG_ISH_PM_AONTASK) || flags & SYSTEM_RESET_HARD) {
		ish_mia_reset();
		__builtin_unreachable();
	}

	system_encode_save_flags(flags, &save_flags);

	if (flags & SYSTEM_RESET_AP_WATCHDOG)
		save_flags |= RESET_FLAG_WATCHDOG;

	chip_save_reset_flags(save_flags);

	ish_persistent_data_commit();
	ish_pm_reset();
	__builtin_unreachable();
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

static void fabric_isr(void)
{
	/**
	 * clear fabric error status, otherwise it will wakeup ISH immediately
	 * when entered low power mode.
	 * TODO(b:130740646): figure out why this issue happens.
	 */
	if (FABRIC_AGENT_STATUS & FABRIC_MIA_STATUS_BIT_ERR)
		FABRIC_AGENT_STATUS = FABRIC_AGENT_STATUS;
}

DECLARE_IRQ(ISH_FABRIC_IRQ, fabric_isr);
