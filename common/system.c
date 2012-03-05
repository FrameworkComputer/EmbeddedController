/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : common functions */

#include "console.h"
#include "host_command.h"
#include "lpc_commands.h"
#include "system.h"
#include "uart.h"
#include "util.h"
#include "version.h"

static enum system_reset_cause_t reset_cause = SYSTEM_RESET_UNKNOWN;

enum system_reset_cause_t system_get_reset_cause(void)
{
	return reset_cause;
}


void system_set_reset_cause(enum system_reset_cause_t cause)
{
	reset_cause = cause;
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
	int copy = ((uint32_t)system_get_image_copy - CONFIG_FLASH_BASE) /
		   CONFIG_FW_IMAGE_SIZE;
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

	/* Load the appropriate reset vector */
	if (copy == SYSTEM_IMAGE_RW_A)
		init_addr = *(uint32_t *)(CONFIG_FW_A_OFF + 4);
#ifndef CONFIG_NO_RW_B
	else if (copy == SYSTEM_IMAGE_RW_B)
		init_addr = *(uint32_t *)(CONFIG_FW_B_OFF + 4);
#endif
	else
		return EC_ERROR_UNKNOWN;

	/* TODO: sanity checks (crosbug.com/p/7468)
	 *
	 * Fail if called outside of pre-init.
	 *
	 * Fail if reboot reason is not soft reboot.  Power-on
	 * reset cause must run RO firmware; if it wants to move to RW
	 * firmware, it must go through a soft reboot first
	 *
	 * Sanity check reset vector; must be inside the appropriate
	 * image. */

	/* Jump to the reset vector */
	resetvec = (void(*)(void))init_addr;
	resetvec();

	/* Should never get here */
	return EC_ERROR_UNIMPLEMENTED;
}


const char *system_get_version(enum system_image_copy_t copy)
{
	int imoffset;
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
#ifndef CONFIG_NO_RW_B
	case SYSTEM_IMAGE_RW_B:
		imoffset = CONFIG_FW_B_OFF;
		break;
#endif
	default:
		return "";
	}

	/* The version string is always located after the reset vectors */
	v = (const struct version_struct *)((uint8_t *)&version_data
		+ imoffset);
	if (v->cookie1 == version_data.cookie1 &&
	    v->cookie2 == version_data.cookie2)
		return v->version;

	return "";
}

const char *system_get_build_info(void)
{
	return build_info;
}

/*****************************************************************************/
/* Console commands */

static int command_sysinfo(int argc, char **argv)
{
	uart_printf("Reset cause: %d (%s)\n",
		    system_get_reset_cause(),
		    system_get_reset_cause_string());
	uart_printf("Scratchpad: 0x%08x\n", system_get_scratchpad());
	uart_printf("Firmware copy: %s\n", system_get_image_copy_string());
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sysinfo, command_sysinfo);


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
DECLARE_CONSOLE_COMMAND(setscratchpad, command_set_scratchpad);


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
DECLARE_CONSOLE_COMMAND(hibernate, command_hibernate);


static int command_version(int argc, char **argv)
{
	uart_printf("RO version:   %s\n",
		    system_get_version(SYSTEM_IMAGE_RO));
	uart_printf("RW-A version: %s\n",
		    system_get_version(SYSTEM_IMAGE_RW_A));
	uart_printf("RW-B version: %s\n",
		    system_get_version(SYSTEM_IMAGE_RW_B));
	uart_printf("Current build: %s\n", system_get_build_info());
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(version, command_version);

/*****************************************************************************/
/* Host commands */

static enum lpc_status host_command_get_version(uint8_t *data)
{
	struct lpc_response_get_version *r =
			(struct lpc_response_get_version *)data;

	strzcpy(r->version_string_ro, system_get_version(SYSTEM_IMAGE_RO),
		sizeof(r->version_string_ro));
	strzcpy(r->version_string_rw_a, system_get_version(SYSTEM_IMAGE_RW_A),
		sizeof(r->version_string_rw_a));
	strzcpy(r->version_string_rw_b, system_get_version(SYSTEM_IMAGE_RW_B),
		sizeof(r->version_string_rw_b));

	switch(system_get_image_copy()) {
	case SYSTEM_IMAGE_RO:
		r->current_image = EC_LPC_IMAGE_RO;
		break;
	case SYSTEM_IMAGE_RW_A:
		r->current_image = EC_LPC_IMAGE_RW_A;
		break;
	case SYSTEM_IMAGE_RW_B:
		r->current_image = EC_LPC_IMAGE_RW_B;
		break;
	default:
		r->current_image = EC_LPC_IMAGE_UNKNOWN;
		break;
	}

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_GET_VERSION, host_command_get_version);


static enum lpc_status host_command_build_info(uint8_t *data)
{
	struct lpc_response_get_build_info *r =
			(struct lpc_response_get_build_info *)data;

	strzcpy(r->build_string, system_get_build_info(),
		sizeof(r->build_string));

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_GET_BUILD_INFO, host_command_build_info);
