/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : hardware specific implementation */

#include "common.h"
#include "console.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "cpu.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

const char *system_get_chip_vendor(void)
{
	return "nrf";
}

const char *system_get_chip_name(void)
{
	return "nrf51822";
}

const char *system_get_chip_revision(void)
{
	return "";
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	if (board_hibernate)
		board_hibernate();

	/* Flush console before hibernating */
	cflush();

	/* chip specific standby mode */
	CPRINTS("TODO: implement %s()", __func__);
}


static void check_reset_cause(void)
{
	uint32_t flags = 0;
	uint32_t raw_cause = NRF51_POWER_RESETREAS;

	if (raw_cause & NRF51_POWER_RESETREAS_RESETPIN)
		flags |= RESET_FLAG_RESET_PIN;

	if (raw_cause & NRF51_POWER_RESETREAS_DOG)
		flags |= RESET_FLAG_WATCHDOG;

	/* Note that the programmer uses a soft reset in debug mode. */
	if (raw_cause & NRF51_POWER_RESETREAS_SREQ)
		flags |= RESET_FLAG_SOFT;

	if (raw_cause & (NRF51_POWER_RESETREAS_OFF |
			NRF51_POWER_RESETREAS_LPCOMP))
		flags |= RESET_FLAG_WAKE_PIN;

	if (raw_cause & (NRF51_POWER_RESETREAS_LOCKUP |
			NRF51_POWER_RESETREAS_DIF))
		flags |= RESET_FLAG_OTHER;

	system_set_reset_flags(flags);

	/* clear it by writing 1's */
	NRF51_POWER_RESETREAS = raw_cause;
}

static void system_watchdog_reset(void)
{
	if (NRF51_WDT_TIMEOUT != 0) {
		/* Hard reset the WDT */
		NRF51_WDT_POWER = 0;
		NRF51_WDT_POWER = 1;
	}

	/* NRF51_WDT_CONFIG_HALT_RUN breaks this */
	NRF51_WDT_CONFIG = NRF51_WDT_CONFIG_SLEEP_RUN;

	NRF51_WDT_RREN = NRF51_WDT_RREN_BIT(0);
	NRF51_WDT_CRV = 3; /* @32KHz */
	NRF51_WDT_START = 1;
}

void system_reset(int flags)
{
	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	if (flags & SYSTEM_RESET_HARD)
		/* Ask the watchdog to trigger a hard reboot */
		system_watchdog_reset();
	else {
		/* Use SYSRESETREQ to trigger a soft reboot */
		CPU_NVIC_APINT = 0x05fa0004;
	}

	/* Spin and wait for reboot; should never return */
	while (1)
		;
}

int system_get_vbnvcontext(uint8_t *block)
{
	CPRINTS("TODO: implement %s()", __func__);
	return EC_ERROR_UNIMPLEMENTED;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	CPRINTS("TODO: implement %s()", __func__);
	return EC_ERROR_UNIMPLEMENTED;
}

void system_pre_init(void)
{
	check_reset_cause();
}
