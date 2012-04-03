/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : common functions */

#include "console.h"
#include "host_command.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"
#include "version.h"


/* Data passed between the current image and the next one when jumping between
 * images. */
#define JUMP_DATA_MAGIC 0x706d754a  /* "Jump" */
#define JUMP_DATA_VERSION 1
struct jump_data {
	/* Add new fields to the _start_ of the struct, since we copy it to the
	 * _end_ of RAM between images.  This way, the magic number will always
	 * be the last word in RAM regardless of how many fields are added. */
	int reset_cause;  /* Reset cause for the previous boot */
	int version;      /* Version (JUMP_DATA_VERSION) */
	int magic;        /* Magic number (JUMP_DATA_MAGIC) */
};


static enum system_reset_cause_t reset_cause = SYSTEM_RESET_UNKNOWN;
static int jumped_to_image;


enum system_reset_cause_t system_get_reset_cause(void)
{
	return reset_cause;
}


int system_jumped_to_this_image(void)
{
	return jumped_to_image;
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


/* Returns true if the given range is overlapped with the active image.
 *
 * We only care the runtime code since the EC is running over it.
 * We don't care about the vector table, FMAP, and init code.
 * Read core/$CORE/ec.lds.S about the below extern symbols.
 */
int system_unsafe_to_overwrite(uint32_t offset, uint32_t size) {
	int copy = ((uint32_t)system_unsafe_to_overwrite - CONFIG_FLASH_BASE) /
	           CONFIG_FW_IMAGE_SIZE;
	uint32_t r_offset = copy * CONFIG_FW_IMAGE_SIZE;
	uint32_t r_size = CONFIG_FW_IMAGE_SIZE;

	if ((offset >= r_offset && offset < (r_offset + r_size)) ||
	    (r_offset >= offset && r_offset < (offset + size)))
		return 1;
	else
		return 0;
}


const char *system_get_image_copy_string(void)
{
	static const char * const copy_descs[] = {"unknown", "RO", "A", "B"};
	int copy = system_get_image_copy();
	return copy < ARRAY_SIZE(copy_descs) ? copy_descs[copy] : "?";
}


/* Jump to what we hope is the init address of an image.  This function does
 * not return. */
static void jump_to_image(uint32_t init_addr)
{
	void (*resetvec)(void) = (void(*)(void))init_addr;
	struct jump_data *jdata = (struct jump_data *)
		(CONFIG_RAM_BASE + CONFIG_RAM_SIZE - sizeof(struct jump_data));

	/* Flush UART output unless the UART hasn't been initialized yet */
	if (uart_init_done())
		uart_flush_output();

	/* Disable interrupts before jump */
	interrupt_disable();

	/* Fill in preserved data between jumps */
	jdata->magic = JUMP_DATA_MAGIC;
	jdata->version = JUMP_DATA_VERSION;
	jdata->reset_cause = reset_cause;

	/* Jump to the reset vector */
	resetvec();
}


int system_run_image_copy(enum system_image_copy_t copy)
{
	uint32_t base;
	uint32_t init_addr;

	/* TODO: sanity checks (crosbug.com/p/7468)
	 *
	 * For this to be allowed either WP must be disabled, or ALL of the
	 * following must be true:
	 *  - We must currently be running the RO image.
	 *  - We must still be in init (that is, before task_start().
	 *  - The target image must be A or B. */

	/* Load the appropriate reset vector */
	switch (copy) {
	case SYSTEM_IMAGE_RO:
		base = CONFIG_FW_RO_OFF;
		break;
	case SYSTEM_IMAGE_RW_A:
		base = CONFIG_FW_A_OFF;
		break;
#ifndef CONFIG_NO_RW_B
	case SYSTEM_IMAGE_RW_B:
		base = CONFIG_FW_B_OFF;
		break;
#endif
	default:
		return EC_ERROR_INVAL;
	}

	/* Make sure the reset vector is inside the destination image */
	init_addr = *(uint32_t *)(base + 4);
	if (init_addr < base || init_addr >= base + CONFIG_FW_IMAGE_SIZE)
		return EC_ERROR_UNKNOWN;

	jump_to_image(init_addr);

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


int system_common_pre_init(void)
{
	struct jump_data *jdata = (struct jump_data *)
		(CONFIG_RAM_BASE + CONFIG_RAM_SIZE - sizeof(struct jump_data));

	/* Check jump data if this is a jump between images */
	if (jdata->magic == JUMP_DATA_MAGIC &&
	    jdata->version == JUMP_DATA_VERSION &&
	    reset_cause == SYSTEM_RESET_SOFT_WARM) {
		/* Yes, we jumped to this image */
		jumped_to_image = 1;
		/* Overwrite the reset cause with the real one */
		reset_cause = jdata->reset_cause;
		/* Clear the jump struct's magic number */
		jdata->magic = 0;
	}

	return EC_SUCCESS;
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
	uart_printf("Jumped to this copy: %s\n",
		    system_jumped_to_this_image() ? "yes" : "no");
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


static int command_sysjump(int argc, char **argv)
{
	uint32_t addr;
	char *e;

	/* TODO: (crosbug.com/p/7468) For this command to be allowed, WP must
	 * be disabled. */

	if (argc < 2) {
		uart_puts("Usage: sysjump <RO | A | B | addr>\n");
		return EC_ERROR_INVAL;
	}

	/* Handle named images */
	if (!strcasecmp(argv[1], "RO")) {
		uart_puts("Jumping directly to RO image...\n");
		return system_run_image_copy(SYSTEM_IMAGE_RO);
	} else if (!strcasecmp(argv[1], "A")) {
		uart_puts("Jumping directly to image A...\n");
		return system_run_image_copy(SYSTEM_IMAGE_RW_A);
	} else if (!strcasecmp(argv[1], "B")) {
		uart_puts("Jumping directly to image B...\n");
		return system_run_image_copy(SYSTEM_IMAGE_RW_B);
	}

	/* Check for arbitrary address */
	addr = strtoi(argv[1], &e, 0);
	if (e && *e) {
		uart_puts("Invalid image address\n");
		return EC_ERROR_INVAL;
	}
	uart_printf("Jumping directly to 0x%08x...\n", addr);
	uart_flush_output();
	jump_to_image(addr);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sysjump, command_sysjump);


static int command_reboot(int argc, char **argv)
{
	uart_puts("Rebooting!\n\n\n");
	uart_flush_output();
	system_reset(1);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(reboot, command_reboot);

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

	switch (system_get_image_copy()) {
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


#ifdef CONFIG_REBOOT_EC
static void clean_busy_bits(void) {
#ifdef CONFIG_LPC
	lpc_send_host_response(0, EC_LPC_RESULT_SUCCESS);
	lpc_send_host_response(1, EC_LPC_RESULT_SUCCESS);
#endif
}

enum lpc_status host_command_reboot(uint8_t *data)
{
	struct lpc_params_reboot_ec *p =
		(struct lpc_params_reboot_ec *)data;

	/* TODO: (crosbug.com/p/7468) For this command to be allowed, WP must
	 * be disabled. */

	switch (p->target) {
	case EC_LPC_IMAGE_RO:
		uart_puts("[Rebooting to image RO!\n]");
		clean_busy_bits();
		system_run_image_copy(SYSTEM_IMAGE_RO);
		break;
	case EC_LPC_IMAGE_RW_A:
		uart_puts("[Rebooting to image A!]\n");
		clean_busy_bits();
		system_run_image_copy(SYSTEM_IMAGE_RW_A);
		break;
	case EC_LPC_IMAGE_RW_B:
		uart_puts("[Rebooting to image B!]\n");
		clean_busy_bits();
		system_run_image_copy(SYSTEM_IMAGE_RW_B);
		break;
	default:
		return EC_LPC_RESULT_ERROR;
	}

	/* We normally never get down here, because we'll have jumped to
	 * another image.  To confirm this command worked, the host will need
	 * to check what image is current using GET_VERSION.
	 *
	 * If we DO get down here, something went wrong in the reboot, so
	 * return error. */
	return EC_LPC_RESULT_ERROR;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_REBOOT_EC, host_command_reboot);
#endif /* CONFIG_REBOOT_EC */
