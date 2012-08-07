/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : LM4 hardware specific implementation */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"

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

/*
 * Time it takes wait_for_hibctl_wc() to return.  Experimentally verified to
 * be ~200 us; the value below is somewhat conservative. */
#define HIB_WAIT_USEC 1000

static int wait_for_hibctl_wc(void)
{
	int i;
	/* Wait for write-capable */
	for (i = 0; i < 1000000; i++) {
		if (LM4_HIBERNATE_HIBCTL & LM4_HIBCTL_WRC)
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
 * Read the real-time clock.
 *
 * @param ss_ptr       Destination for sub-seconds value, if not null.
 *
 * @return the real-time clock seconds value.
 */
uint32_t system_get_rtc(uint32_t *ss_ptr)
{
	uint32_t rtc, rtc2;
	uint32_t rtcss;

	/*
	 * The hibernate module isn't synchronized, so need to read repeatedly
	 * to guarantee a valid read.
	 */
	do {
		rtc = LM4_HIBERNATE_HIBRTCC;
		rtcss = LM4_HIBERNATE_HIBRTCSS & 0x7fff;
		rtc2 = LM4_HIBERNATE_HIBRTCC;
	} while (rtc != rtc2);

	if (ss_ptr)
		*ss_ptr = rtcss;

	return rtc;
}

/**
 * Set the real-time clock.
 *
 * @param seconds	New clock value.
 */
void system_set_rtc(uint32_t seconds)
{
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCLD = seconds;
	wait_for_hibctl_wc();
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
	uint32_t rtc, rtcss;
	uint32_t hibctl;

	/* Store hibernate flags */
	hibdata_write(HIBDATA_INDEX_WAKE, flags);

	/* Clear pending interrupt */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIC = LM4_HIBERNATE_HIBRIS;

	/* TODO: PRESERVE RESET FLAGS */

	/* TODO: If sleeping forever, only wake on wake pin. */

	/* Add expected overhead for hibernate register writes */
	microseconds += HIB_WAIT_USEC * 4;

	/*
	 * The code below must run uninterrupted to make sure we accurately
	 * calculate the RTC match value.
	 */
	interrupt_disable();

	/*
	 * Calculate the wake match, compensating for additional delays caused
	 * by writing to the hibernate register.
	 */
	rtc = system_get_rtc(&rtcss) + seconds;
	rtcss += microseconds * (32768/64) / (1000000/64);
	if (rtcss > 0x7fff) {
		rtc += rtcss >> 15;
		rtcss &= 0x7fff;
	}

	/* Set RTC alarm match */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCM0 = rtc;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCSS = rtcss << 16;

	/* Go to hibernation and wake on RTC match or WAKE pin */
	hibctl = (LM4_HIBERNATE_HIBCTL | LM4_HIBCTL_RTCWEN |
		  LM4_HIBCTL_PINWEN | LM4_HIBCTL_HIBREQ);
	wait_for_hibctl_wc();
	__enter_hibernate(hibctl);
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
	 * Enable the hibernation oscillator, if it's not already enabled.
	 * This should only need setting if the EC completely lost power (for
	 * example, the battery was pulled).
	 */
	if (!(LM4_HIBERNATE_HIBCTL & LM4_HIBCTL_CLK32EN)) {
		int i;

		/* Enable clock to hibernate module */
		wait_for_hibctl_wc();
		LM4_HIBERNATE_HIBCTL |= LM4_HIBCTL_CLK32EN;

		/* Wait for write-complete */
		for (i = 0; i < 1000000; i++) {
			if (LM4_HIBERNATE_HIBRIS & 0x10)
				break;
		}

		/* Enable and reset RTC */
		wait_for_hibctl_wc();
		LM4_HIBERNATE_HIBCTL |= LM4_HIBCTL_RTCEN;
		system_set_rtc(0);

		/* Clear all hibernate data entries */
		for (i = 0; i < LM4_HIBERNATE_HIBDATA_ENTRIES; i++)
			hibdata_write(i, 0);
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
	uint32_t save_flags = 0;

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		save_flags = system_get_reset_flags() | RESET_FLAG_PRESERVED;

	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		save_flags |= RESET_FLAG_AP_OFF;

	hibdata_write(HIBDATA_INDEX_SAVED_RESET_FLAGS, save_flags);

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

/*****************************************************************************/
/* Console commands */

static int command_system_rtc(int argc, char **argv)
{
	uint32_t rtc;
	uint32_t rtcss;

	if (argc == 3 && !strcasecmp(argv[1], "set")) {
		char *e;
		uint32_t t = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		system_set_rtc(t);
	} else if (argc > 1) {
		return EC_ERROR_INVAL;
	}

	rtc = system_get_rtc(&rtcss);
	ccprintf("RTC: 0x%08x.%04x (%d.%06d s)\n",
		 rtc, rtcss, rtc, (rtcss * (1000000/64)) / (32768/64));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc, command_system_rtc,
			"[set <seconds>]",
			"Get/set real-time clock",
			NULL);

/*****************************************************************************/
/* Host commands */

static int system_rtc_get_value(struct host_cmd_handler_args *args)
{
	struct ec_response_rtc *r = args->response;

	r->time = system_get_rtc(NULL);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_GET_VALUE,
		     system_rtc_get_value,
		     EC_VER_MASK(0));

static int system_rtc_set_value(struct host_cmd_handler_args *args)
{
	const struct ec_params_rtc *p = args->params;

	system_set_rtc(p->time);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RTC_SET_VALUE,
		     system_rtc_set_value,
		     EC_VER_MASK(0));
