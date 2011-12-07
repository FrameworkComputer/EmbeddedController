/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC */

#include "console.h"
#include "registers.h"
#include "system.h"
#include "uart.h"
#include "util.h"
#include "version.h"

/* Forward declarations for console commands */
static int command_sysinfo(int argc, char **argv);
static int command_set_scratchpad(int argc, char **argv);
static int command_hibernate(int argc, char **argv);

static const struct console_command console_commands[] = {
	{"setscratch", command_set_scratchpad},
	{"sysinfo", command_sysinfo},
	{"hibernate", command_hibernate}
};
static const struct console_group command_group = {
	"System", console_commands, ARRAY_SIZE(console_commands)
};

struct version_struct {
	uint32_t cookie1;
	char version[32];
	uint32_t cookie2;
} __attribute__ ((packed));

static const struct version_struct version_data = {
	0xce112233,
	CROS_EC_VERSION_STRING,
	0xce445566
};

static uint32_t raw_reset_cause = 0;
static enum system_reset_cause_t reset_cause = SYSTEM_RESET_UNKNOWN;


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
	enum system_image_copy_t copy = system_get_image_copy();
	uint32_t hib_status = LM4_HIBERNATE_HIBRIS;

	/* Read the raw reset cause */
	raw_reset_cause = LM4_SYSTEM_RESC;

	if (hib_status & 0x0d) {
		/* the hibernation module wakes up the system */
		if (hib_status & 0x8)
			reset_cause = SYSTEM_RESET_WAKE_PIN;
		else if (hib_status & 0x1)
			reset_cause = SYSTEM_RESET_RTC_ALARM;
		else if (hib_status & 0x4)
			reset_cause = SYSTEM_RESET_LOW_BATTERY;
		/* clear the pending interrupt */
		wait_for_hibctl_wc();
		LM4_HIBERNATE_HIBIC = hib_status;
	} else if (copy == SYSTEM_IMAGE_RW_A || copy == SYSTEM_IMAGE_RW_B) {
		/* If we're in image A or B, the only way we can get there is
		 * via a warm reset. */
		reset_cause = SYSTEM_RESET_SOFT_WARM;
	} else if (raw_reset_cause & 0x28) {
		/* Watchdog timer 0 or 1 */
		reset_cause = SYSTEM_RESET_WATCHDOG;
	} else if (raw_reset_cause & 0x10) {
		reset_cause = SYSTEM_RESET_SOFT_COLD;
	} else if (raw_reset_cause & 0x04) {
		reset_cause = SYSTEM_RESET_BROWNOUT;
	} else if (raw_reset_cause & 0x02) {
		reset_cause = SYSTEM_RESET_POWER_ON;
	} else if (raw_reset_cause & 0x01) {
		reset_cause = SYSTEM_RESET_RESET_PIN;
	} else if (raw_reset_cause) {
		reset_cause = SYSTEM_RESET_OTHER;
	} else {
		reset_cause = SYSTEM_RESET_UNKNOWN;
	}
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
	LM4_HIBERNATE_HIBCTL = 0x5B;
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
	/* initialize properly registers after reset (cf errata) */
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBRTCT = 0x7fff;
	wait_for_hibctl_wc();
	LM4_HIBERNATE_HIBIM = 0;

	check_reset_cause();

	return EC_SUCCESS;
}


int system_init(void)
{
	/* Clear the hardware reset cause, now that we've committed to running
	 * this image. */
	LM4_SYSTEM_RESC = 0;

	/* Register our internal commands */
	return console_register_commands(&command_group);
}


enum system_reset_cause_t system_get_reset_cause(void)
{
	return reset_cause;
}


const char *system_get_reset_cause_string(void)
{
	static const char * const cause_descs[] = {
		"unknown", "other", "brownout", "power-on", "reset pin",
		"soft cold", "soft warm", "watchdog", "rtc alarm", "wake pin",
		"low battery"};

	return reset_cause < ARRAY_SIZE(cause_descs) ?
			cause_descs[reset_cause] : "?";
}


enum system_image_copy_t system_get_image_copy(void)
{
	int copy = (uint32_t)system_get_image_copy / CONFIG_FW_IMAGE_SIZE;
	switch (copy) {
	case 0:
		return SYSTEM_IMAGE_RO;
	case 1:
		return SYSTEM_IMAGE_RW_A;
	case 2:
		return SYSTEM_IMAGE_RW_B;
	default:
		return SYSTEM_IMAGE_UNKNOWN;
	}
}


const char *system_get_image_copy_string(void)
{
	static const char * const copy_descs[] = {"unknown", "RO", "A", "B"};
	int copy = system_get_image_copy();
	return copy < ARRAY_SIZE(copy_descs) ? copy_descs[copy] : "?";
}


int system_run_image_copy(enum system_image_copy_t copy)
{
	uint32_t init_addr;
	void (*resetvec)(void);

	/* Fail if we're not in RO firmware */
	if (system_get_image_copy() != SYSTEM_IMAGE_RO)
		return EC_ERROR_UNKNOWN;

	/* TODO: fail if we've already started up interrupts, etc. */

	/* Load the appropriate reset vector */
	if (copy == SYSTEM_IMAGE_RW_A)
		init_addr = *(uint32_t *)(CONFIG_FW_A_OFF + 4);
	else if (copy == SYSTEM_IMAGE_RW_B)
		init_addr = *(uint32_t *)(CONFIG_FW_B_OFF + 4);
	else
		return EC_ERROR_UNKNOWN;

	/* TODO: sanity check reset vector; must be inside the
	 * appropriate image. */

	/* Jump to the reset vector */
	resetvec = (void(*)(void))init_addr;
	resetvec();

	/* Should never get here */
	return EC_ERROR_UNIMPLEMENTED;
}


int system_reset(int is_cold)
{
	/* TODO - warm vs. cold */
	LM4_NVIC_APINT = 0x05fa0004;

	/* Spin and wait for reboot; should never return */
	/* TODO: should disable task swaps while waiting */
	while (1) {}

	return EC_ERROR_UNKNOWN;
}


int system_set_scratchpad(uint32_t value)
{
	int rv;

	/* Wait for ok-to-write */
	rv = wait_for_hibctl_wc();
	if (rv != EC_SUCCESS)
		return rv;

	/* Write scratchpad */
	/* TODO: might be more elegant to have a write_hibernate_reg() method
	 * which takes an address and data and does the delays */
	LM4_HIBERNATE_HIBDATA = value;

	/* Wait for write-complete */
	return wait_for_hibctl_wc();
}


uint32_t system_get_scratchpad(void)
{
	return LM4_HIBERNATE_HIBDATA;
}


const char *system_get_version(enum system_image_copy_t copy)
{
	int imoffset;
	const uint32_t *p, *pend;
	const struct version_struct *v;

	/* Handle version of current image */
	if (copy == system_get_image_copy() || copy == SYSTEM_IMAGE_UNKNOWN)
		return version_data.version;

	switch (copy) {
	case SYSTEM_IMAGE_RO:
		imoffset = CONFIG_FW_RO_OFF;
		break;
	case SYSTEM_IMAGE_RW_A:
		imoffset = CONFIG_FW_A_OFF;
		break;
	case SYSTEM_IMAGE_RW_B:
		imoffset = CONFIG_FW_B_OFF;
		break;
	default:
		return "";
	}

	/* Search for version cookies in target image */
	/* TODO: could be smarter about where to search if we stuffed
	 * the version data into a predefined area of the image - for
	 * example, immediately following the reset vectors. */
	pend = (uint32_t *)(imoffset + CONFIG_FW_IMAGE_SIZE
			    - sizeof(version_data));
	for (p = (uint32_t *)imoffset; p <= pend; p++) {
		v = (const struct version_struct *)p;
		if (v->cookie1 == version_data.cookie1 &&
		    v->cookie2 == version_data.cookie2)
			return v->version;
	}

	return "";
}


static int command_sysinfo(int argc, char **argv)
{
	uart_printf("Reset cause: %d (%s)\n",
		    system_get_reset_cause(),
		    system_get_reset_cause_string());
	uart_printf("Raw reset cause: 0x%x\n", raw_reset_cause);
	uart_printf("Scratchpad: 0x%08x\n", system_get_scratchpad());
	uart_printf("Firmware copy: %s\n", system_get_image_copy_string());
	return EC_SUCCESS;
}


static int command_set_scratchpad(int argc, char **argv)
{
	int s;
	char *e;

	if (argc < 2) {
		uart_puts("Usage: scratchpad <value>\n");
		return EC_ERROR_UNKNOWN;
	}

	s = strtoi(argv[1], &e, 0);
	if (*e) {
		uart_puts("Invalid scratchpad value\n");
		return EC_ERROR_UNKNOWN;
	}
	uart_printf("Setting scratchpad to 0x%08x\n", s);
	return  system_set_scratchpad(s);
}

static int command_hibernate(int argc, char **argv)
{
	int seconds;
	int microseconds = 0;

	if (argc < 2) {
		uart_puts("Usage: hibernate <seconds> [<microseconds>]\n");
		return EC_ERROR_UNKNOWN;
	}
	seconds = strtoi(argv[1], NULL, 0);
	if (argc >= 3)
		microseconds = strtoi(argv[2], NULL, 0);

	uart_printf("Hibernating for %d.%06d s ...\n", seconds, microseconds);
	uart_flush_output();

	system_hibernate(seconds, microseconds);

	return EC_SUCCESS;
}
