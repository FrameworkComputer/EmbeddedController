/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Rotor MCU */

#include "common.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "watchdog.h"

void system_pre_init(void)
{
}

void system_reset(int flags)
{
	/* Disable interrupts to avoid task swaps during reboot. */
	interrupt_disable();

	/* TODO: Implement flags and stuff. */

	/*
	 * Try to trigger a watchdog reset, by setting the smallest timeout
	 * period we can.
	 */
	ROTOR_MCU_WDT_TORR = 0;
	watchdog_reload();

	/* Wait for system reset. */
	while (1)
		asm("wfi");
}

const char *system_get_chip_name(void)
{
	return "rotor";
}

const char *system_get_chip_vendor(void)
{
	return "";
}

const char *system_get_chip_revision(void)
{
	return "";
}

int system_get_vbnvcontext(uint8_t *block)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	return EC_ERROR_UNIMPLEMENTED;

}
