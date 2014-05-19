/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : common functions */
#include "clock.h"
#include "common.h"
#include "console.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lpc.h"
#ifdef CONFIG_MPU
#include "mpu.h"
#endif
#include "panic.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "version.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* Round up to a multiple of 4 */
#define ROUNDUP4(x) (((x) + 3) & ~3)

/* Data for an individual jump tag */
struct jump_tag {
	uint16_t tag;		/* Tag ID */
	uint8_t data_size;	/* Size of data which follows */
	uint8_t data_version;	/* Data version */

	/* Followed by data_size bytes of data */
};

/*
 * Data passed between the current image and the next one when jumping between
 * images.
 */
#define JUMP_DATA_MAGIC 0x706d754a  /* "Jump" */
#define JUMP_DATA_VERSION 3
#define JUMP_DATA_SIZE_V2 16  /* Size of version 2 jump data struct */
struct jump_data {
	/*
	 * Add new fields to the _start_ of the struct, since we copy it to the
	 * _end_ of RAM between images.  This way, the magic number will always
	 * be the last word in RAM regardless of how many fields are added.
	 */

	/* Fields from version 3 */
	uint8_t reserved0;    /* (used in proto1 to signal recovery mode) */
	int struct_size;      /* Size of struct jump_data */

	/* Fields from version 2 */
	int jump_tag_total;   /* Total size of all jump tags */

	/* Fields from version 1 */
	uint32_t reset_flags; /* Reset flags from the previous boot */
	int version;          /* Version (JUMP_DATA_VERSION) */
	int magic;            /* Magic number (JUMP_DATA_MAGIC).  If this
			       * doesn't match at pre-init time, assume no valid
			       * data from the previous image. */
};

/* Jump data (at end of RAM, or preceding panic data) */
static struct jump_data *jdata;

/*
 * Reset flag descriptions.  Must be in same order as bits of RESET_FLAG_
 * constants.
 */
static const char * const reset_flag_descs[] = {
	"other", "reset-pin", "brownout", "power-on", "watchdog", "soft",
	"hibernate", "rtc-alarm", "wake-pin", "low-battery", "sysjump",
	"hard", "ap-off", "preserved"};

static const char * const image_names[] = {"unknown", "RO", "RW"};
static uint32_t reset_flags;
static int jumped_to_image;
static int disable_jump;  /* Disable ALL jumps if system is locked */
static int force_locked;  /* Force system locked even if WP isn't enabled */
static enum ec_reboot_cmd reboot_at_shutdown;

/* On-going actions preventing going into deep-sleep mode */
uint32_t sleep_mask;

int system_is_locked(void)
{
	if (force_locked)
		return 1;

#ifdef CONFIG_SYSTEM_UNLOCKED
	/* System is explicitly unlocked */
	return 0;

#elif defined(CONFIG_FLASH)
	/*
	 * Unlocked if write protect pin deasserted or read-only firmware
	 * is not protected.
	 */
	if ((EC_FLASH_PROTECT_GPIO_ASSERTED | EC_FLASH_PROTECT_RO_NOW) &
	    ~flash_get_protect())
		return 0;

	/* If WP pin is asserted and lock is applied, we're locked */
	return 1;
#else
	/* Other configs are locked by default */
	return 1;
#endif
}

test_mockable uintptr_t system_usable_ram_end(void)
{
	/* Leave space at the end of RAM for jump data and tags.
	 *
	 * Note that jump_tag_total is 0 on a reboot, so we have the maximum
	 * amount of RAM available on a reboot; we only lose space for stored
	 * tags after a sysjump.  When verified boot runs after a reboot, it'll
	 * have as much RAM as we can give it; after verified boot jumps to
	 * another image there'll be less RAM, but we'll care less too. */
	return (uintptr_t)jdata - jdata->jump_tag_total;
}

uint32_t system_get_reset_flags(void)
{
	return reset_flags;
}

void system_set_reset_flags(uint32_t flags)
{
	reset_flags |= flags;
}

void system_clear_reset_flags(uint32_t flags)
{
	reset_flags &= ~flags;
}

void system_print_reset_flags(void)
{
	int count = 0;
	int i;

	if (!reset_flags) {
		CPUTS("unknown");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(reset_flag_descs); i++) {
		if (reset_flags & (1 << i)) {
			if (count++)
				CPUTS(" ");

			CPUTS(reset_flag_descs[i]);
		}
	}
}

int system_jumped_to_this_image(void)
{
	return jumped_to_image;
}

int system_add_jump_tag(uint16_t tag, int version, int size, const void *data)
{
	struct jump_tag *t;

	/* Only allowed during a sysjump */
	if (!jdata || jdata->magic != JUMP_DATA_MAGIC)
		return EC_ERROR_UNKNOWN;

	/* Make room for the new tag */
	if (size > 255)
		return EC_ERROR_INVAL;
	jdata->jump_tag_total += ROUNDUP4(size) + sizeof(struct jump_tag);

	t = (struct jump_tag *)system_usable_ram_end();
	t->tag = tag;
	t->data_size = size;
	t->data_version = version;
	if (size)
		memcpy(t + 1, data, size);

	return EC_SUCCESS;
}

const uint8_t *system_get_jump_tag(uint16_t tag, int *version, int *size)
{
	const struct jump_tag *t;
	int used = 0;

	if (!jdata)
		return NULL;

	/* Search through tag data for a match */
	while (used < jdata->jump_tag_total) {
		/* Check the next tag */
		t = (const struct jump_tag *)(system_usable_ram_end() + used);
		used += sizeof(struct jump_tag) + ROUNDUP4(t->data_size);
		if (t->tag != tag)
			continue;

		/* Found a match */
		if (size)
			*size = t->data_size;
		if (version)
			*version = t->data_version;

		return (const uint8_t *)(t + 1);
	}

	/* If we're still here, no match */
	return NULL;
}

void system_disable_jump(void)
{
	disable_jump = 1;

#ifdef CONFIG_MPU
	if (system_is_locked()) {
		int ret;
		int enable_mpu = 0;
		enum system_image_copy_t copy;

		CPRINTS("MPU type: %08x", mpu_get_type());
		/*
		 * Protect RAM from code execution
		 */
		ret = mpu_protect_ram();
		if (ret == EC_SUCCESS) {
			enable_mpu = 1;
			CPRINTS("RAM locked. Exclusion %08x-%08x",
				&__iram_text_start, &__iram_text_end);
		} else {
			CPRINTS("Failed to lock RAM (%d)", ret);
		}

		/*
		 * Protect inactive image (ie. RO if running RW, vice versa)
		 * from code execution.
		 */
		switch (system_get_image_copy()) {
		case SYSTEM_IMAGE_RO:
			ret =  mpu_lock_rw_flash();
			copy = SYSTEM_IMAGE_RW;
			break;
		case SYSTEM_IMAGE_RW:
			ret =  mpu_lock_ro_flash();
			copy = SYSTEM_IMAGE_RO;
			break;
		default:
			copy = SYSTEM_IMAGE_UNKNOWN;
			ret = !EC_SUCCESS;
		}
		if (ret == EC_SUCCESS) {
			enable_mpu = 1;
			CPRINTS("%s image locked", image_names[copy]);
		} else {
			CPRINTS("Failed to lock %s image (%d)",
				image_names[copy], ret);
		}

		if (enable_mpu)
			mpu_enable();
	} else {
		CPRINTS("System is unlocked. Skip MPU configuration");
	}
#endif
}

test_mockable enum system_image_copy_t system_get_image_copy(void)
{
	uintptr_t my_addr = (uintptr_t)system_get_image_copy -
			    CONFIG_FLASH_BASE;

	if (my_addr >= CONFIG_FW_RO_OFF &&
	    my_addr < (CONFIG_FW_RO_OFF + CONFIG_FW_RO_SIZE))
		return SYSTEM_IMAGE_RO;

	if (my_addr >= CONFIG_FW_RW_OFF &&
	    my_addr < (CONFIG_FW_RW_OFF + CONFIG_FW_RW_SIZE))
		return SYSTEM_IMAGE_RW;

	return SYSTEM_IMAGE_UNKNOWN;
}

int system_get_image_used(enum system_image_copy_t copy)
{
	const uint8_t *image;
	int size = 0;

	if (copy == SYSTEM_IMAGE_RO) {
		image = (const uint8_t *)CONFIG_FW_RO_OFF;
		size = CONFIG_FW_RO_SIZE;
	} else if (copy == SYSTEM_IMAGE_RW) {
		image = (const uint8_t *)CONFIG_FW_RW_OFF;
		size = CONFIG_FW_RW_SIZE;
	}

	if (size <= 0)
		return 0;

	/*
	 * Scan backwards looking for 0xea byte, which is by definition the
	 * last byte of the image.  See ec.lds.S for how this is inserted at
	 * the end of the image.
	 */
	for (size--; size > 0 && image[size] != 0xea; size--)
		;

	return size ? size + 1 : 0;  /* 0xea byte IS part of the image */
}

test_mockable int system_unsafe_to_overwrite(uint32_t offset, uint32_t size)
{
	uint32_t r_offset;
	uint32_t r_size;

	switch (system_get_image_copy()) {
	case SYSTEM_IMAGE_RO:
		r_offset = CONFIG_FW_RO_OFF;
		r_size = CONFIG_FW_RO_SIZE;
		break;
	case SYSTEM_IMAGE_RW:
		r_offset = CONFIG_FW_RW_OFF;
		r_size = CONFIG_FW_RW_SIZE;
		break;
	default:
		return 0;
	}

	if ((offset >= r_offset && offset < (r_offset + r_size)) ||
	    (r_offset >= offset && r_offset < (offset + size)))
		return 1;
	else
		return 0;
}

const char *system_get_image_copy_string(void)
{
	int copy = system_get_image_copy();
	return copy < ARRAY_SIZE(image_names) ? image_names[copy] : "?";
}

/**
 * Jump to what we hope is the init address of an image.
 *
 * This function does not return.
 *
 * @param init_addr	Init address of target image
 */
static void jump_to_image(uintptr_t init_addr)
{
	void (*resetvec)(void) = (void(*)(void))init_addr;

	/*
	 * Jumping to any image asserts the signal to the Silego chip that that
	 * EC is not in read-only firmware.  (This is not technically true if
	 * jumping from RO -> RO, but that's not a meaningful use case...).
	 *
	 * Pulse the signal long enough to set the latch in the Silego, then
	 * drop it again so we don't leak power through the pulldown in the
	 * Silego.
	 */
	gpio_set_level(GPIO_ENTERING_RW, 1);
	usleep(MSEC);
	gpio_set_level(GPIO_ENTERING_RW, 0);

	/* Flush UART output unless the UART hasn't been initialized yet */
	if (uart_init_done())
		uart_flush_output();

	/* Disable interrupts before jump */
	interrupt_disable();

	/* Fill in preserved data between jumps */
	jdata->reserved0 = 0;
	jdata->magic = JUMP_DATA_MAGIC;
	jdata->version = JUMP_DATA_VERSION;
	jdata->reset_flags = reset_flags;
	jdata->jump_tag_total = 0;  /* Reset tags */
	jdata->struct_size = sizeof(struct jump_data);

	/* Call other hooks; these may add tags */
	hook_notify(HOOK_SYSJUMP);

	/* Jump to the reset vector */
	resetvec();
}

/**
 * Return the base pointer for the image copy, or 0xffffffff if error.
 */
static uint32_t get_base(enum system_image_copy_t copy)
{
	switch (copy) {
	case SYSTEM_IMAGE_RO:
		return CONFIG_FLASH_BASE + CONFIG_FW_RO_OFF;
	case SYSTEM_IMAGE_RW:
		return CONFIG_FLASH_BASE + CONFIG_FW_RW_OFF;
	default:
		return 0xffffffff;
	}
}

/**
 * Return the size of the image copy, or 0 if error.
 */
#ifndef EMU_BUILD
static uint32_t get_size(enum system_image_copy_t copy)
{
	switch (copy) {
	case SYSTEM_IMAGE_RO:
		return CONFIG_FW_RO_SIZE;
	case SYSTEM_IMAGE_RW:
		return CONFIG_FW_RW_SIZE;
	default:
		return 0;
	}
}
#endif

int system_run_image_copy(enum system_image_copy_t copy)
{
	uintptr_t base;
	uintptr_t init_addr;

	/* If system is already running the requested image, done */
	if (system_get_image_copy() == copy)
		return EC_SUCCESS;

	if (system_is_locked()) {
		/* System is locked, so disallow jumping between images unless
		 * this is the initial jump from RO to RW code. */

		/* Must currently be running the RO image */
		if (system_get_image_copy() != SYSTEM_IMAGE_RO)
			return EC_ERROR_ACCESS_DENIED;

		/* Target image must be RW image */
		if (copy != SYSTEM_IMAGE_RW)
			return EC_ERROR_ACCESS_DENIED;

		/* Jumping must still be enabled */
		if (disable_jump)
			return EC_ERROR_ACCESS_DENIED;
	}

	/* Load the appropriate reset vector */
	base = get_base(copy);
	if (base == 0xffffffff)
		return EC_ERROR_INVAL;

	/* Make sure the reset vector is inside the destination image */
	init_addr = *(uintptr_t *)(base + 4);
#ifndef EMU_BUILD
	if (init_addr < base || init_addr >= base + get_size(copy))
		return EC_ERROR_UNKNOWN;
#endif

	CPRINTS("Jumping to image %s", image_names[copy]);

	jump_to_image(init_addr);

	/* Should never get here */
	return EC_ERROR_UNKNOWN;
}

const char *system_get_version(enum system_image_copy_t copy)
{
	uintptr_t addr;
	const struct version_struct *v;

	/* Handle version of current image */
	if (copy == system_get_image_copy() || copy == SYSTEM_IMAGE_UNKNOWN)
		return &RO(version_data).version[0];

	addr = get_base(copy);
	if (addr == 0xffffffff)
		return "";

	/* The version string is always located after the reset vectors, so
	 * it's the same as in the current image. */
	addr += ((uintptr_t)&version_data - get_base(system_get_image_copy()));

	/* Make sure the version struct cookies match before returning the
	 * version string. */
	v = (const struct version_struct *)addr;
	if (v->cookie1 == RO(version_data).cookie1 &&
	    v->cookie2 == RO(version_data).cookie2)
		return v->version;

	return "";
}

int system_get_board_version(void)
{
	int v = 0;

#ifdef CONFIG_BOARD_VERSION
	if (gpio_get_level(GPIO_BOARD_VERSION1))
		v |= 0x01;
	if (gpio_get_level(GPIO_BOARD_VERSION2))
		v |= 0x02;
	if (gpio_get_level(GPIO_BOARD_VERSION3))
		v |= 0x04;
#endif

	return v;
}

const char *system_get_build_info(void)
{
	return build_info;
}

void system_common_pre_init(void)
{
	uintptr_t addr;

	/*
	 * Put the jump data before the panic data, or at the end of RAM if
	 * panic data is not present.
	 */
	addr = (uintptr_t)panic_get_data();
	if (!addr)
		addr = CONFIG_RAM_BASE + CONFIG_RAM_SIZE;

	jdata = (struct jump_data *)(addr - sizeof(struct jump_data));

	/*
	 * Check jump data if this is a jump between images.  Jumps all show up
	 * as an unknown reset reason, because we jumped directly from one
	 * image to another without actually triggering a chip reset.
	 */
	if (jdata->magic == JUMP_DATA_MAGIC &&
	    jdata->version >= 1 &&
	    reset_flags == 0) {
		/* Change in jump data struct size between the previous image
		 * and this one. */
		int delta;

		/* Yes, we jumped to this image */
		jumped_to_image = 1;
		/* Restore the reset flags */
		reset_flags = jdata->reset_flags | RESET_FLAG_SYSJUMP;

		/*
		 * If the jump data structure isn't the same size as the
		 * current one, shift the jump tags to immediately before the
		 * current jump data structure, to make room for initalizing
		 * the new fields below.
		 */
		if (jdata->version == 1)
			delta = 0;  /* No tags in v1, so no need for move */
		else if (jdata->version == 2)
			delta = sizeof(struct jump_data) - JUMP_DATA_SIZE_V2;
		else
			delta = sizeof(struct jump_data) - jdata->struct_size;

		if (delta && jdata->jump_tag_total) {
			uint8_t *d = (uint8_t *)system_usable_ram_end();
			memmove(d, d + delta, jdata->jump_tag_total);
		}

		/* Initialize fields added after version 1 */
		if (jdata->version < 2)
			jdata->jump_tag_total = 0;

		/* Initialize fields added after version 2 */
		if (jdata->version < 3)
			jdata->reserved0 = 0;

		/* Struct size is now the current struct size */
		jdata->struct_size = sizeof(struct jump_data);

		/*
		 * Clear the jump struct's magic number.  This prevents
		 * accidentally detecting a jump when there wasn't one, and
		 * disallows use of system_add_jump_tag().
		 */
		jdata->magic = 0;
	} else {
		/* Clear the whole jump_data struct */
		memset(jdata, 0, sizeof(struct jump_data));
	}
}

/**
 * Handle a pending reboot command.
 */
static int handle_pending_reboot(enum ec_reboot_cmd cmd)
{
	switch (cmd) {
	case EC_REBOOT_CANCEL:
		return EC_SUCCESS;
	case EC_REBOOT_JUMP_RO:
		return system_run_image_copy(SYSTEM_IMAGE_RO);
	case EC_REBOOT_JUMP_RW:
		return system_run_image_copy(SYSTEM_IMAGE_RW);
	case EC_REBOOT_COLD:
		system_reset(SYSTEM_RESET_HARD);
		/* That shouldn't return... */
		return EC_ERROR_UNKNOWN;
	case EC_REBOOT_DISABLE_JUMP:
		system_disable_jump();
		return EC_SUCCESS;
	case EC_REBOOT_HIBERNATE:
		CPRINTS("system hibernating");
		system_hibernate(0, 0);
		/* That shouldn't return... */
		return EC_ERROR_UNKNOWN;
	default:
		return EC_ERROR_INVAL;
	}
}

/*****************************************************************************/
/* Hooks */

static void system_common_shutdown(void)
{
	handle_pending_reboot(reboot_at_shutdown);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, system_common_shutdown, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console commands */

static int command_sysinfo(int argc, char **argv)
{
	ccprintf("Reset flags: 0x%08x (", system_get_reset_flags());
	system_print_reset_flags();
	ccprintf(")\n");
	ccprintf("Copy:   %s\n", system_get_image_copy_string());
	ccprintf("Jumped: %s\n", system_jumped_to_this_image() ? "yes" : "no");

	ccputs("Flags: ");
	if (system_is_locked()) {
		ccputs(" locked");
		if (force_locked)
			ccputs(" (forced)");
		if (disable_jump)
			ccputs(" jump-disabled");
	} else
		ccputs(" unlocked");
	ccputs("\n");

	if (reboot_at_shutdown)
		ccprintf("Reboot at shutdown: %d\n", reboot_at_shutdown);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sysinfo, command_sysinfo,
			NULL,
			"Print system info",
			NULL);

#ifdef CONFIG_CMD_SCRATCHPAD
static int command_scratchpad(int argc, char **argv)
{
	int rv = EC_SUCCESS;

	if (argc == 2) {
		char *e;
		int s = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		rv = system_set_scratchpad(s);
	}

	ccprintf("Scratchpad: 0x%08x\n", system_get_scratchpad());
	return rv;
}
DECLARE_CONSOLE_COMMAND(scratchpad, command_scratchpad,
			"[val]",
			"Get or set scratchpad value",
			NULL);
#endif /* CONFIG_CMD_SCRATCHPAD */

static int command_hibernate(int argc, char **argv)
{
	int seconds = 0;
	int microseconds = 0;

	if (argc >= 2)
		seconds = strtoi(argv[1], NULL, 0);
	if (argc >= 3)
		microseconds = strtoi(argv[2], NULL, 0);

	if (seconds || microseconds)
		ccprintf("Hibernating for %d.%06d s\n", seconds, microseconds);
	else
		ccprintf("Hibernating until wake pin asserted.\n");

	system_hibernate(seconds, microseconds);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hibernate, command_hibernate,
			"[sec] [usec]",
			"Hibernate the EC",
			NULL);

static int command_version(int argc, char **argv)
{
	ccprintf("Chip:    %s %s %s\n", system_get_chip_vendor(),
		 system_get_chip_name(), system_get_chip_revision());
	ccprintf("Board:   %d\n", system_get_board_version());
	ccprintf("RO:      %s\n", system_get_version(SYSTEM_IMAGE_RO));
	ccprintf("RW:      %s\n", system_get_version(SYSTEM_IMAGE_RW));
	ccprintf("Build: %s\n", system_get_build_info());
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(version, command_version,
			NULL,
			"Print versions",
			NULL);

static int command_sysjump(int argc, char **argv)
{
	uint32_t addr;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* Handle named images */
	if (!strcasecmp(argv[1], "RO"))
		return system_run_image_copy(SYSTEM_IMAGE_RO);
	else if (!strcasecmp(argv[1], "RW") || !strcasecmp(argv[1], "A")) {
		/*
		 * TODO(crosbug.com/p/11149): remove "A" once all scripts are
		 * updated to use "RW".
		 */
		return system_run_image_copy(SYSTEM_IMAGE_RW);
	} else if (!strcasecmp(argv[1], "disable")) {
		system_disable_jump();
		return EC_SUCCESS;
	}

	/* Arbitrary jumps are only allowed on an unlocked system */
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;

	/* Check for arbitrary address */
	addr = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	ccprintf("Jumping to 0x%08x\n", addr);
	cflush();
	jump_to_image(addr);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sysjump, command_sysjump,
			"[RO | RW | addr | disable]",
			"Jump to a system image or address",
			NULL);

static int command_reboot(int argc, char **argv)
{
	int flags = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcasecmp(argv[i], "hard") ||
		    !strcasecmp(argv[i], "cold")) {
			flags |= SYSTEM_RESET_HARD;
		} else if (!strcasecmp(argv[i], "soft")) {
			flags &= ~SYSTEM_RESET_HARD;
		} else if (!strcasecmp(argv[i], "ap-off")) {
			flags |= SYSTEM_RESET_LEAVE_AP_OFF;
		} else if (!strcasecmp(argv[i], "cancel")) {
			reboot_at_shutdown = EC_REBOOT_CANCEL;
			return EC_SUCCESS;
		} else if (!strcasecmp(argv[i], "preserve")) {
			flags |= SYSTEM_RESET_PRESERVE_FLAGS;
		} else
			return EC_ERROR_PARAM1 + i - 1;
	}

	if (flags & SYSTEM_RESET_HARD)
		ccputs("Hard-");
	ccputs("Rebooting!\n\n\n");
	cflush();
	system_reset(flags);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(reboot, command_reboot,
			"[hard|soft] [preserve] [ap-off] [cancel]",
			"Reboot the EC",
			NULL);

static int command_system_lock(int argc, char **argv)
{
	force_locked = 1;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(syslock, command_system_lock,
			NULL,
			"Lock the system, even if WP is disabled",
			NULL);

#ifdef CONFIG_LOW_POWER_IDLE
/**
 * Modify and print the sleep mask which controls access to deep sleep
 * mode in the idle task.
 */
static int command_sleepmask(int argc, char **argv)
{
	int v;

	if (argc >= 2) {
		if (parse_bool(argv[1], &v)) {
			if (v)
				disable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
			else
				enable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
		} else {
			char *e;
			v = strtoi(argv[1], &e, 10);
			if (*e)
				return EC_ERROR_PARAM1;

			/* Set sleep mask directly. */
			sleep_mask = v;
		}
	}

	ccprintf("sleep mask: %08x\n", sleep_mask);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sleepmask, command_sleepmask,
			"[ on | off | <sleep_mask>]",
			"Display/force sleep mask.\nSee also 'dsleepmask'.",
			NULL);
#endif

#ifdef CONFIG_CMD_JUMPTAGS
static int command_jumptags(int argc, char **argv)
{
	const struct jump_tag *t;
	int used = 0;

	/* Jump tags valid only after a sysjump */
	if (!jdata)
		return EC_SUCCESS;

	while (used < jdata->jump_tag_total) {
		/* Check the next tag */
		t = (const struct jump_tag *)(system_usable_ram_end() + used);
		used += sizeof(struct jump_tag) + ROUNDUP4(t->data_size);

		ccprintf("%08x: 0x%04x %c%c.%d %3d\n",
			 (uintptr_t)t,
			 t->tag, t->tag >> 8, (uint8_t)t->tag,
			 t->data_version, t->data_size);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(jumptags, command_jumptags,
			NULL,
			"List jump tags",
			NULL);
#endif /* CONFIG_CMD_JUMPTAGS */

/*****************************************************************************/
/* Host commands */

static int host_command_get_version(struct host_cmd_handler_args *args)
{
	struct ec_response_get_version *r = args->response;

	strzcpy(r->version_string_ro, system_get_version(SYSTEM_IMAGE_RO),
		sizeof(r->version_string_ro));
	strzcpy(r->version_string_rw, system_get_version(SYSTEM_IMAGE_RW),
		sizeof(r->version_string_rw));

	switch (system_get_image_copy()) {
	case SYSTEM_IMAGE_RO:
		r->current_image = EC_IMAGE_RO;
		break;
	case SYSTEM_IMAGE_RW:
		r->current_image = EC_IMAGE_RW;
		break;
	default:
		r->current_image = EC_IMAGE_UNKNOWN;
		break;
	}

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_VERSION,
		     host_command_get_version,
		     EC_VER_MASK(0));

static int host_command_build_info(struct host_cmd_handler_args *args)
{
	strzcpy(args->response, system_get_build_info(), args->response_max);
	args->response_size = strlen(args->response) + 1;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_BUILD_INFO,
		     host_command_build_info,
		     EC_VER_MASK(0));

static int host_command_get_chip_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_chip_info *r = args->response;

	strzcpy(r->vendor, system_get_chip_vendor(), sizeof(r->vendor));
	strzcpy(r->name, system_get_chip_name(), sizeof(r->name));
	strzcpy(r->revision, system_get_chip_revision(), sizeof(r->revision));

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CHIP_INFO,
		     host_command_get_chip_info,
		     EC_VER_MASK(0));

#ifdef CONFIG_BOARD_VERSION
int host_command_get_board_version(struct host_cmd_handler_args *args)
{
	struct ec_response_board_version *r = args->response;

	r->board_version = (uint16_t) system_get_board_version();

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_BOARD_VERSION,
		     host_command_get_board_version,
		     EC_VER_MASK(0));
#endif

int host_command_vbnvcontext(struct host_cmd_handler_args *args)
{
	const struct ec_params_vbnvcontext *p = args->params;
	struct ec_response_vbnvcontext *r;

	switch (p->op) {
	case EC_VBNV_CONTEXT_OP_READ:
		r = args->response;
		if (system_get_vbnvcontext(r->block))
			return EC_RES_ERROR;
		args->response_size = sizeof(*r);
		break;
	case EC_VBNV_CONTEXT_OP_WRITE:
		if (system_set_vbnvcontext(p->block))
			return EC_RES_ERROR;
		break;
	default:
		return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}

/*
 * TODO(crbug.com/239197) : Adding both versions to the version mask is a
 * temporary workaround for a problem in the cros_ec driver. Drop
 * EC_VER_MASK(0) once cros_ec driver can send the correct version.
 */
DECLARE_HOST_COMMAND(EC_CMD_VBNV_CONTEXT,
		     host_command_vbnvcontext,
		     EC_VER_MASK(EC_VER_VBNV_CONTEXT) | EC_VER_MASK(0));

int host_command_reboot(struct host_cmd_handler_args *args)
{
	struct ec_params_reboot_ec p;

	/*
	 * Ensure reboot parameters don't get clobbered when the response
	 * is sent in case data argument points to the host tx/rx buffer.
	 */
	memcpy(&p, args->params, sizeof(p));

	if (p.cmd == EC_REBOOT_CANCEL) {
		/* Cancel pending reboot */
		reboot_at_shutdown = EC_REBOOT_CANCEL;
		return EC_RES_SUCCESS;
	} else if (p.flags & EC_REBOOT_FLAG_ON_AP_SHUTDOWN) {
		/* Store request for processing at chipset shutdown */
		reboot_at_shutdown = p.cmd;
		return EC_RES_SUCCESS;
	}

#ifdef HAS_TASK_HOSTCMD
	if (p.cmd == EC_REBOOT_JUMP_RO ||
	    p.cmd == EC_REBOOT_JUMP_RW ||
	    p.cmd == EC_REBOOT_COLD ||
	    p.cmd == EC_REBOOT_HIBERNATE) {
		/* Clean busy bits on host for commands that won't return */
		args->result = EC_RES_SUCCESS;
		host_send_response(args);
	}
#endif

	CPRINTS("Executing host reboot command %d", p.cmd);
	switch (handle_pending_reboot(p.cmd)) {
	case EC_SUCCESS:
		return EC_RES_SUCCESS;
	case EC_ERROR_INVAL:
		return EC_RES_INVALID_PARAM;
	case EC_ERROR_ACCESS_DENIED:
		return EC_RES_ACCESS_DENIED;
	default:
		return EC_RES_ERROR;
	}
}
DECLARE_HOST_COMMAND(EC_CMD_REBOOT_EC,
		     host_command_reboot,
		     EC_VER_MASK(0));
