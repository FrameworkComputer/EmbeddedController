/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : LM4 hardware specific implementation */

#include "common.h"
#include "cpu.h"
#include "registers.h"
#include "system.h"
#include "task.h"

/* Indices for hibernate data registers */
enum hibdata_index {
	HIBDATA_INDEX_SCRATCHPAD,        /* General-purpose scratchpad */
	HIBDATA_INDEX_WAKE,              /* Wake reasons for hibernate */
	HIBDATA_INDEX_SAVED_RESET_FLAGS  /* Saved reset flags */
};

/* Flags for HIBDATA_INDEX_WAKE */
#define HIBDATA_WAKE_RTC        (1 << 0)  /* RTC alarm */
#define HIBDATA_WAKE_HARD_RESET (1 << 1)  /* Hard reset via short RTC alarm */
#define HIBDATA_WAKE_PIN        (1 << 2)  /* Wake pin */

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

/**
 * Read hibernate register at specified index.
 *
 * @return The value of the register or 0 if invalid index.
 */
static uint32_t hibdata_read(enum hibdata_index index)
{
	if (index < 0 || index >= LM4_HIBERNATE_HIBDATA_ENTRIES)
		return 0;

	return LM4_HIBERNATE_HIBDATA[index];
}

/**
 * Write hibernate register at specified index.
 *
 * @return nonzero if error.
 */
static int hibdata_write(enum hibdata_index index, uint32_t value)
{
	int rv;

	if (index < 0 || index >= LM4_HIBERNATE_HIBDATA_ENTRIES)
		return EC_ERROR_INVAL;

	/* Wait for ok-to-write */
	rv = wait_for_hibctl_wc();
	if (rv != EC_SUCCESS)
		return rv;

	/* Write register */
	LM4_HIBERNATE_HIBDATA[index] = value;

	/* Wait for write-complete */
	return wait_for_hibctl_wc();
}

static void check_reset_cause(void)
{
	uint32_t hib_status = LM4_HIBERNATE_HIBRIS;
	uint32_t raw_reset_cause = LM4_SYSTEM_RESC;
	uint32_t hib_wake_flags = hibdata_read(HIBDATA_INDEX_WAKE);
	uint32_t flags = 0;

	/* Clear the reset causes now that we've read them */
	LM4_SYSTEM_RESC = 0;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIC = hib_status;
	hibdata_write(HIBDATA_INDEX_WAKE, 0);

	if (raw_reset_cause & 0x02) {
		/*
		 * Full power-on reset of chip.  This resets the flash
		 * protection registers to their permanently-stored values.
		 * Note that this is also triggered by hibernation, because
		 * that de-powers the chip.
		 */
		flags |= RESET_FLAG_POWER_ON;
	} else if (!flags && (raw_reset_cause & 0x01)) {
		/*
		 * LM4 signals the reset pin in RESC for all power-on resets,
		 * even though the external pin wasn't asserted.  Make setting
		 * this flag mutually-exclusive with power on flag, so we can
		 * use it to indicate a keyboard-triggered reset.
		 */
		flags |= RESET_FLAG_RESET_PIN;
	}

	if (raw_reset_cause & 0x04)
		flags |= RESET_FLAG_BROWNOUT;

	if (raw_reset_cause & 0x10)
		flags |= RESET_FLAG_SOFT;

	if (raw_reset_cause & 0x28) {
		/* Watchdog timer 0 or 1 */
		flags |= RESET_FLAG_WATCHDOG;
	}

	/* Handle other raw reset causes */
	if (raw_reset_cause && !flags)
		flags |= RESET_FLAG_OTHER;


	if ((hib_status & 0x09) &&
	    (hib_wake_flags & HIBDATA_WAKE_HARD_RESET)) {
		/* Hibernation caused by software-triggered hard reset */
		flags |= RESET_FLAG_HARD;

		/* Consume the hibernate reasons so we don't see them below */
		hib_status &= ~0x09;
	}

	if ((hib_status & 0x01) && (hib_wake_flags & HIBDATA_WAKE_RTC))
		flags |= RESET_FLAG_RTC_ALARM;

	if ((hib_status & 0x08) && (hib_wake_flags & HIBDATA_WAKE_PIN))
		flags |= RESET_FLAG_WAKE_PIN;

	if (hib_status & 0x04)
		flags |= RESET_FLAG_LOW_BATTERY;

	/* Restore then clear saved reset flags */
	flags |= hibdata_read(HIBDATA_INDEX_SAVED_RESET_FLAGS);
	hibdata_write(HIBDATA_INDEX_SAVED_RESET_FLAGS, 0);

	system_set_reset_flags(flags);
}

/*
 * A3 and earlier chip stepping has a problem accessing flash during shutdown.
 * To work around that, we jump to RAM before hibernating.  This function must
 * live in RAM.  It must be called with interrupts disabled, cannot call other
 * functions, and can't be declared static (or else the compiler optimizes it
 * into the main hibernate function.
 */
void  __attribute__((section(".iram.text"))) __enter_hibernate(int hibctl)
{
	LM4_HIBERNATE_HIBCTL = hibctl;
	while (1)
		;
}

/**
 * Internal hibernate function.
 *
 * @param seconds      Number of seconds to sleep before RTC alarm
 * @param microseconds Number of microseconds to sleep before RTC alarm
 * @param flags        Hibernate wake flags
 */
static void hibernate(uint32_t seconds, uint32_t microseconds, uint32_t flags)
{
	/* Store hibernate flags */
	hibdata_write(HIBDATA_INDEX_WAKE, flags);

	/* Clear pending interrupt */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIC = LM4_HIBERNATE_HIBRIS;

	/* TODO: PRESERVE RESET FLAGS */

	/* TODO: If sleeping forever, only wake on wake pin. */

	/* Set RTC alarm match */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCM0 = seconds;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCSS = (microseconds * 512 / 15625) << 16;

	/* Start counting toward the alarm */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCLD = 0;

	/* Go to hibernation and wake on RTC match or WAKE pin */
	wait_for_hibctl_wc();
	__enter_hibernate(0x5B);
}


void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	hibernate(seconds, microseconds, HIBDATA_WAKE_RTC | HIBDATA_WAKE_PIN);
}

int system_pre_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable clocks to the hibernation module */
	LM4_SYSTEM_RCGCHIB = 1;
	/* Wait 3 clock cycles before using the module */
	scratch = LM4_SYSTEM_RCGCHIB;

	/*
	 * Enable the hibernation oscillator, if it's not already enabled.  We
	 * use this to hold our scratchpad value across reboots.
	 */
	if (!(LM4_HIBERNATE_HIBCTL & 0x40)) {
		int i;

		/* Enable clock to hibernate module */
		wait_for_hibctl_wc();
		LM4_HIBERNATE_HIBCTL |= 0x40;

		/* Wait for write-complete */
		for (i = 0; i < 1000000; i++) {
			if (LM4_HIBERNATE_HIBRIS & 0x10)
				break;
		}
	}

	/*
	 * Initialize registers after reset to work around LM4 chip errata
	 * (still present in A3 chip stepping).
	 */
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

void system_reset(int flags)
{
	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		hibdata_write(HIBDATA_INDEX_SAVED_RESET_FLAGS,
			      system_get_reset_flags());
	else
		hibdata_write(HIBDATA_INDEX_SAVED_RESET_FLAGS, 0);

	if (flags & SYSTEM_RESET_HARD) {
		/* Bounce through hibernate to trigger a hard reboot */
		hibernate(0, 50000, HIBDATA_WAKE_HARD_RESET);
	} else
		CPU_NVIC_APINT = 0x05fa0004;

	/* Spin and wait for reboot; should never return */
	while (1)
		;
}

int system_set_scratchpad(uint32_t value)
{
	return hibdata_write(HIBDATA_INDEX_SCRATCHPAD, value);
}

uint32_t system_get_scratchpad(void)
{
	return hibdata_read(HIBDATA_INDEX_SCRATCHPAD);
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
