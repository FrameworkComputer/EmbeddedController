/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : LM4 hardware specific implementation */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "host_command.h"
#include "panic.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Indices for hibernate data registers */
enum hibdata_index {
	HIBDATA_INDEX_SCRATCHPAD,           /* General-purpose scratchpad */
	HIBDATA_INDEX_WAKE,                 /* Wake reasons for hibernate */
	HIBDATA_INDEX_SAVED_RESET_FLAGS,    /* Saved reset flags */
#ifdef CONFIG_SOFTWARE_PANIC
	HIBDATA_INDEX_SAVED_PANIC_REASON,   /* Saved panic reason */
	HIBDATA_INDEX_SAVED_PANIC_INFO,     /* Saved panic data */
	HIBDATA_INDEX_SAVED_PANIC_EXCEPTION /* Saved panic exception code */
#endif
};

/* Flags for HIBDATA_INDEX_WAKE */
#define HIBDATA_WAKE_RTC        (1 << 0)  /* RTC alarm */
#define HIBDATA_WAKE_HARD_RESET (1 << 1)  /* Hard reset via short RTC alarm */
#define HIBDATA_WAKE_PIN        (1 << 2)  /* Wake pin */

/*
 * Time to hibernate to trigger a power-on reset.  50 ms is sufficient for the
 * EC itself, but we need a longer delay to ensure the rest of the components
 * on the same power rail are reset and 5VALW has dropped.
 */
#define HIB_RESET_USEC 1000000

/*
 * Convert between microseconds and the hibernation module RTC subsecond
 * register which has 15-bit resolution. Divide down both numerator and
 * denominator to avoid integer overflow while keeping the math accurate.
 */
#define HIB_RTC_USEC_TO_SUBSEC(us) ((us) * (32768/64) / (1000000/64))
#define HIB_RTC_SUBSEC_TO_USEC(ss) ((ss) * (1000000/64) / (32768/64))

/**
 * Wait for a write to commit to a hibernate register.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
static int wait_for_hibctl_wc(void)
{
	int i;

	/* Wait for write-capable */
	for (i = 0; i < 1000000; i++) {
		if (LM4_HIBERNATE_HIBCTL & LM4_HIBCTL_WRC)
			return EC_SUCCESS;
	}
	return EC_ERROR_TIMEOUT;
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
void  __attribute__((noinline)) __attribute__((section(".iram.text")))
__enter_hibernate(int hibctl)
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
uint32_t system_get_rtc_sec_subsec(uint32_t *ss_ptr)
{
	uint32_t rtc, rtc2;
	uint32_t rtcss, rtcss2;

	/*
	 * The hibernate module isn't synchronized, so need to read repeatedly
	 * to guarantee a valid read.
	 */
	do {
		rtc = LM4_HIBERNATE_HIBRTCC;
		rtcss = LM4_HIBERNATE_HIBRTCSS & 0x7fff;
		rtcss2 = LM4_HIBERNATE_HIBRTCSS & 0x7fff;
		rtc2 = LM4_HIBERNATE_HIBRTCC;
	} while (rtc != rtc2 || rtcss != rtcss2);

	if (ss_ptr)
		*ss_ptr = rtcss;

	return rtc;
}

timestamp_t system_get_rtc(void)
{
	uint32_t rtc, rtc_ss;
	timestamp_t time;

	rtc = system_get_rtc_sec_subsec(&rtc_ss);

	time.val = ((uint64_t)rtc) * SECOND + HIB_RTC_SUBSEC_TO_USEC(rtc_ss);
	return time;
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
 * Set the hibernate RTC match time at a given time from now
 *
 * @param seconds      Number of seconds from now for RTC match
 * @param microseconds Number of microseconds from now for RTC match
 */
static void set_hibernate_rtc_match_time(uint32_t seconds,
					uint32_t microseconds)
{
	uint32_t rtc, rtcss;

	/*
	 * Make sure that the requested delay is not less then the
	 * amount of time it takes to set the RTC match registers,
	 * otherwise, the match event could be missed.
	 */
	if (seconds == 0 && microseconds < HIB_SET_RTC_MATCH_DELAY_USEC)
		microseconds = HIB_SET_RTC_MATCH_DELAY_USEC;

	/* Calculate the wake match */
	rtc = system_get_rtc_sec_subsec(&rtcss) + seconds;
	rtcss += HIB_RTC_USEC_TO_SUBSEC(microseconds);
	if (rtcss > 0x7fff) {
		rtc += rtcss >> 15;
		rtcss &= 0x7fff;
	}

	/* Set RTC alarm match */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCM0 = rtc;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCSS = rtcss << 16;
	wait_for_hibctl_wc();
}

/**
 * Use hibernate module to set up an RTC interrupt at a given
 * time from now
 *
 * @param seconds      Number of seconds before RTC interrupt
 * @param microseconds Number of microseconds before RTC interrupt
 */
void system_set_rtc_alarm(uint32_t seconds, uint32_t microseconds)
{
	/* Clear pending interrupt */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIC = LM4_HIBERNATE_HIBRIS;

	/* Set match time */
	set_hibernate_rtc_match_time(seconds, microseconds);

	/* Enable RTC interrupt on match */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIM = 1;

	/*
	 * Wait for the write to commit.  This ensures that the RTC interrupt
	 * actually gets enabled.  This is important if we're about to switch
	 * the system to the 30 kHz oscillator, which might prevent the write
	 * from comitting.
	 */
	wait_for_hibctl_wc();
}

/**
 * Disable and clear the RTC interrupt.
 */
void system_reset_rtc_alarm(void)
{
	/* Disable hibernate interrupts */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIM = 0;

	/* Clear interrupts */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIC = LM4_HIBERNATE_HIBRIS;
}

/**
 * Hibernate module interrupt
 */
void __hibernate_irq(void)
{
	system_reset_rtc_alarm();
}
DECLARE_IRQ(LM4_IRQ_HIBERNATE, __hibernate_irq, 1);

/**
 * Enable hibernate interrupt
 */
void system_enable_hib_interrupt(void)
{
	task_enable_irq(LM4_IRQ_HIBERNATE);
}

/**
 * Internal hibernate function.
 *
 * @param seconds      Number of seconds to sleep before RTC alarm
 * @param microseconds Number of microseconds to sleep before RTC alarm
 * @param flags        Additional hibernate wake flags
 */
static void hibernate(uint32_t seconds, uint32_t microseconds, uint32_t flags)
{
	uint32_t hibctl;

	/* Set up wake reasons and hibernate flags */
	hibctl = LM4_HIBERNATE_HIBCTL | LM4_HIBCTL_PINWEN;

	if (flags & HIBDATA_WAKE_PIN)
		hibctl |= LM4_HIBCTL_PINWEN;
	else
		hibctl &= ~LM4_HIBCTL_PINWEN;

	if (seconds || microseconds) {
		hibctl |= LM4_HIBCTL_RTCWEN;
		flags |= HIBDATA_WAKE_RTC;

		set_hibernate_rtc_match_time(seconds, microseconds);

		/* Enable RTC interrupt on match */
		wait_for_hibctl_wc();
		LM4_HIBERNATE_HIBIM = 1;
	} else {
		hibctl &= ~LM4_HIBCTL_RTCWEN;
	}
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBCTL = hibctl;

	/* Clear pending interrupt */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIC = LM4_HIBERNATE_HIBRIS;

	/* Store hibernate flags */
	hibdata_write(HIBDATA_INDEX_WAKE, flags);

	__enter_hibernate(hibctl | LM4_HIBCTL_HIBREQ);
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/* Flush console before hibernating */
	cflush();
	hibernate(seconds, microseconds, HIBDATA_WAKE_PIN);
}

void system_pre_init(void)
{
	uint32_t hibctl;
#ifdef CONFIG_SOFTWARE_PANIC
	uint32_t reason, info;
	uint8_t exception;
#endif

	/*
	 * Enable clocks to the hibernation module in run, sleep,
	 * and deep sleep modes.
	 */
	clock_enable_peripheral(CGC_OFFSET_HIB, 0x1, CGC_MODE_ALL);

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
	 * Set wake reasons to RTC match and WAKE pin by default.
	 * Before going in to hibernate, these may change.
	 */
	hibctl = LM4_HIBERNATE_HIBCTL;
	hibctl |= LM4_HIBCTL_RTCWEN;
	hibctl |= LM4_HIBCTL_PINWEN;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBCTL = hibctl;

	/*
	 * Initialize registers after reset to work around LM4 chip errata
	 * (still present in A3 chip stepping).
	 */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCT = 0x7fff;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIM = 0;

	check_reset_cause();

#ifdef CONFIG_SOFTWARE_PANIC
	/* Restore then clear saved panic reason */
	reason = hibdata_read(HIBDATA_INDEX_SAVED_PANIC_REASON);
	info = hibdata_read(HIBDATA_INDEX_SAVED_PANIC_INFO);
	exception = hibdata_read(HIBDATA_INDEX_SAVED_PANIC_EXCEPTION);
	if (reason || info || exception) {
		panic_set_reason(reason, info, exception);
		hibdata_write(HIBDATA_INDEX_SAVED_PANIC_REASON, 0);
		hibdata_write(HIBDATA_INDEX_SAVED_PANIC_INFO, 0);
		hibdata_write(HIBDATA_INDEX_SAVED_PANIC_EXCEPTION, 0);
	}
#endif

	/* Initialize bootcfg if needed */
	if (LM4_SYSTEM_BOOTCFG != CONFIG_BOOTCFG_VALUE) {
		/* read-modify-write */
		LM4_FLASH_FMD = (LM4_SYSTEM_BOOTCFG_MASK & LM4_SYSTEM_BOOTCFG)
			| (~LM4_SYSTEM_BOOTCFG_MASK & CONFIG_BOOTCFG_VALUE);
		LM4_FLASH_FMA = 0x75100000;
		LM4_FLASH_FMC = 0xa4420008;  /* WRKEY | COMT */
		while (LM4_FLASH_FMC & 0x08)
			;
	}

	/* Brown-outs should trigger a reset */
	LM4_SYSTEM_PBORCTL |= 0x02;
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
#ifdef CONFIG_SOFTWARE_PANIC
		uint32_t reason, info;
		uint8_t exception;

		/* Panic data will be wiped by hard reset, so save it */
		panic_get_reason(&reason, &info, &exception);
		hibdata_write(HIBDATA_INDEX_SAVED_PANIC_REASON, reason);
		hibdata_write(HIBDATA_INDEX_SAVED_PANIC_INFO, info);
		hibdata_write(HIBDATA_INDEX_SAVED_PANIC_EXCEPTION, exception);
#endif

		/*
		 * Bounce through hibernate to trigger a hard reboot.  Do
		 * not wake on wake pin, since we need the full duration.
		 */
		hibernate(0, HIB_RESET_USEC, HIBDATA_WAKE_HARD_RESET);
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

static char to_hex(int x)
{
	if (x >= 0 && x <= 9)
		return '0' + x;
	return 'a' + x - 10;
}

const char *system_get_chip_id_string(void)
{
	static char str[15] = "Unknown-";
	char *p = str + 8;
	uint32_t did = LM4_SYSTEM_DID1 >> 16;

	if (*p)
		return (const char *)str;

	*p = to_hex(did >> 12);
	*(p + 1) = to_hex((did >> 8) & 0xf);
	*(p + 2) = to_hex((did >> 4) & 0xf);
	*(p + 3) = to_hex(did & 0xf);
	*(p + 4) = '\0';

	return (const char *)str;
}

const char *system_get_raw_chip_name(void)
{
	switch ((LM4_SYSTEM_DID1 & 0xffff0000) >> 16) {
	case 0x10de:
		return "tm4e1g31h6zrb";
	case 0x10e2:
		return "lm4fsxhh5bb";
	case 0x10e3:
		return "lm4fs232h5bb";
	case 0x10e4:
		return "lm4fs99h5bb";
	case 0x10e6:
		return "lm4fs1ah5bb";
	case 0x10ea:
		return "lm4fs1gh5bb";
	default:
		return system_get_chip_id_string();
	}
}

const char *system_get_chip_name(void)
{
	const char *postfix = "-tm"; /* test mode */
	static char str[20];
	const char *raw_chip_name = system_get_raw_chip_name();
	char *p = str;

	if (LM4_TEST_MODE_ENABLED) {
		/* Debug mode is enabled. Postfix chip name. */
		while (*raw_chip_name)
			*(p++) = *(raw_chip_name++);
		while (*postfix)
			*(p++) = *(postfix++);
		*p = '\0';
		return (const char *)str;
	} else {
		return raw_chip_name;
	}
}

int system_get_vbnvcontext(uint8_t *block)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	return EC_ERROR_UNIMPLEMENTED;
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
#ifdef CONFIG_CMD_RTC
void print_system_rtc(enum console_channel ch)
{
	uint32_t rtc;
	uint32_t rtcss;

	rtc = system_get_rtc_sec_subsec(&rtcss);
	cprintf(ch, "RTC: 0x%08x.%04x (%d.%06d s)\n",
		 rtc, rtcss, rtc, HIB_RTC_SUBSEC_TO_USEC(rtcss));
}

static int command_system_rtc(int argc, char **argv)
{
	if (argc == 3 && !strcasecmp(argv[1], "set")) {
		char *e;
		uint32_t t = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		system_set_rtc(t);
	} else if (argc > 1) {
		return EC_ERROR_INVAL;
	}

	print_system_rtc(CC_COMMAND);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc, command_system_rtc,
			"[set <seconds>]",
			"Get/set real-time clock");

#ifdef CONFIG_CMD_RTC_ALARM
/**
 * Test the RTC alarm by setting an interrupt on RTC match.
 */
static int command_rtc_alarm_test(int argc, char **argv)
{
	int s = 1, us = 0;
	char *e;

	ccprintf("Setting RTC alarm\n");
	system_enable_hib_interrupt();

	if (argc > 1) {
		s = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;

	}
	if (argc > 2) {
		us = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM2;

	}

	system_set_rtc_alarm(s, us);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rtc_alarm, command_rtc_alarm_test,
			"[seconds [microseconds]]",
			"Test alarm");
#endif /* CONFIG_CMD_RTC_ALARM */
#endif /* CONFIG_CMD_RTC */

/*****************************************************************************/
/* Host commands */

#ifdef CONFIG_HOSTCMD_RTC
static int system_rtc_get_value(struct host_cmd_handler_args *args)
{
	struct ec_response_rtc *r = args->response;

	r->time = system_get_rtc_sec_subsec(NULL);
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
#endif /* CONFIG_HOSTCMD_RTC */
