/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : hardware specific implementation */

#include "board.h"
#include "cpu.h"
#include "registers.h"
#include "system.h"
#include "task.h"


static int wait_for_hibctl_wc(void)
{
	int i;
	/* Wait for write-capable */
	for (i = 0; i < 1000000; i++) {
		if (LM4_HIBERNATE_HIBCTL & 0x80000000)
			return EC_SUCCESS;
	}
	return EC_ERROR_UNKNOWN;
}


static void check_reset_cause(void)
{
	uint32_t hib_status = LM4_HIBERNATE_HIBRIS;
	enum system_reset_cause_t reset_cause = SYSTEM_RESET_UNKNOWN;
	uint32_t raw_reset_cause;

	/* Read and clear the raw reset cause */
	raw_reset_cause = LM4_SYSTEM_RESC;
	LM4_SYSTEM_RESC = 0;

	if (hib_status & 0x0d) {
		/* The hibernation module woke up the system */
		if (hib_status & 0x8)
			reset_cause = SYSTEM_RESET_WAKE_PIN;
		else if (hib_status & 0x1)
			/* Note that system_reset(1) also triggers this reset
			 * cause, because it uses hibernate with a RTC wake to
			 * trigger a power-on reset. */
			reset_cause = SYSTEM_RESET_RTC_ALARM;
		else if (hib_status & 0x4)
			reset_cause = SYSTEM_RESET_LOW_BATTERY;
		/* Clear the pending interrupt */
		wait_for_hibctl_wc();
		LM4_HIBERNATE_HIBIC = hib_status;
	} else if (raw_reset_cause & 0x28) {
		/* Watchdog timer 0 or 1 */
		reset_cause = SYSTEM_RESET_WATCHDOG;
	} else if (raw_reset_cause & 0x10) {
		reset_cause = SYSTEM_RESET_SOFT;
	} else if (raw_reset_cause & 0x04) {
		reset_cause = SYSTEM_RESET_BROWNOUT;
	} else if (raw_reset_cause & 0x02) {
		reset_cause = SYSTEM_RESET_POWER_ON;
	} else if (raw_reset_cause & 0x01) {
		reset_cause = SYSTEM_RESET_RESET_PIN;
	} else if (raw_reset_cause) {
		reset_cause = SYSTEM_RESET_OTHER;
	}
	system_set_reset_cause(reset_cause);
}


void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/* clear pending interrupt */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIC = LM4_HIBERNATE_HIBRIS;
	/* set RTC alarm match */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCM0 = seconds;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCSS = (microseconds * 512 / 15625) << 16;

	/* start counting toward the alarm */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCLD = 0;
	/* go to hibernation and wake on RTC match or WAKE pin */
	wait_for_hibctl_wc();

#ifdef BOARD_link
	if (system_get_board_version() == BOARD_VERSION_PROTO1) {
		/* Need VDD3ON because we can't drop VDD externally */
		LM4_HIBERNATE_HIBCTL = 0x15B;
	} else {
		/* EVT+ can drop VDD */
		LM4_HIBERNATE_HIBCTL = 0x5B;
	}
#else
	LM4_HIBERNATE_HIBCTL = 0x5B;
#endif

	/* we are going to hibernate ... */
	while (1) ;
}


int system_pre_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable clocks to the hibernation module */
	LM4_SYSTEM_RCGCHIB = 1;
	/* Wait 3 clock cycles before using the module */
	scratch = LM4_SYSTEM_RCGCHIB;

	/* Enable the hibernation oscillator, if it's not already enabled.  We
	 * use this to hold our scratchpad value across reboots. */
	if (!(LM4_HIBERNATE_HIBCTL & 0x40)) {
		int rv, i;
		rv = wait_for_hibctl_wc();
		if (rv != EC_SUCCESS)
			return rv;

		/* Enable clock to hibernate module */
		LM4_HIBERNATE_HIBCTL |= 0x40;
		/* Wait for write-complete */
		for (i = 0; i < 1000000; i++) {
			if (LM4_HIBERNATE_HIBRIS & 0x10)
				break;
		}
	}

	/* Initialize registers after reset (cf errata) */
	/* TODO: fixed in A3 chip stepping? */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCT = 0x7fff;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIM = 0;

	check_reset_cause();

	/* Initialize bootcfg if needed */
	if (LM4_SYSTEM_BOOTCFG != BOOTCFG_VALUE) {
		LM4_FLASH_FMD = BOOTCFG_VALUE;
		LM4_FLASH_FMA = 0x75100000;
		LM4_FLASH_FMC = 0xa4420008;  /* WRKEY | COMT */
		while (LM4_FLASH_FMC & 0x08)
			;
	}

	/* Brown-outs should trigger a reset */
	LM4_SYSTEM_PBORCTL |= 0x02;

	return EC_SUCCESS;
}


void system_reset(int is_hard)
{
	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	if (is_hard) {
		/* Bounce through hibernate to trigger a hard reboot */
		system_hibernate(0, 50000);
	} else {
		/* Soft reboot */
		CPU_NVIC_APINT = 0x05fa0004;
	}

	/* Spin and wait for reboot; should never return */
	while (1) {}
}


int system_set_scratchpad(uint32_t value)
{
	int rv;

	/* Wait for ok-to-write */
	rv = wait_for_hibctl_wc();
	if (rv != EC_SUCCESS)
		return rv;

	/* Write scratchpad */
	/* TODO: (crosbug.com/p/7472) might be more elegant to have a
	 * write_hibernate_reg() method which takes an address and
	 * data and does the delays.  Then we could move the hibernate
	 * register accesses to a separate module. */
	LM4_HIBERNATE_HIBDATA = value;

	/* Wait for write-complete */
	return wait_for_hibctl_wc();
}


uint32_t system_get_scratchpad(void)
{
	return LM4_HIBERNATE_HIBDATA;
}


const char *system_get_chip_vendor(void)
{
	return "ti";
}


const char *system_get_chip_name(void)
{
	if ((LM4_SYSTEM_DID1 & 0xffff0000) == 0x10e20000) {
		return "lm4fsxhh5bb";
	} else if ((LM4_SYSTEM_DID1 & 0xffff0000) == 0x10e30000) {
		return "lm4fs232h5bb";
	} else if ((LM4_SYSTEM_DID1 & 0xffff0000) == 0x10e40000) {
		return "lm4fs99h5bb";
	} else if ((LM4_SYSTEM_DID1 & 0xffff0000) == 0x10e60000) {
		return "lm4fs1ah5bb";
	} else if ((LM4_SYSTEM_DID1 & 0xffff0000) == 0x10ea0000) {
		return "lm4fs1gh5bb";
	} else {
		return "";
	}
}


const char *system_get_chip_revision(void)
{
	static char rev[3];

	/* Extract the major[15:8] and minor[7:0] revisions. */
	rev[0] = 'A' + ((LM4_SYSTEM_DID0 >> 8) & 0xff);
	rev[1] = '0' + (LM4_SYSTEM_DID0 & 0xff);
	rev[2] = 0;

	return rev;
}
