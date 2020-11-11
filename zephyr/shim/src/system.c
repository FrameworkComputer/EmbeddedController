/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <string.h>
#include <sys/util.h>

#include "config.h"
#include "ec_commands.h"
#include "sysjump.h"
#include "system.h"

/* Ongoing actions preventing going into deep-sleep mode. */
uint32_t sleep_mask;

/* Round up to a multiple of 4. */
#define ROUNDUP4(x) (((x) + 3) & ~3)

/** Data for an individual jump tag. */
struct jump_tag {
	/** Tag ID. */
	uint16_t tag;
	/** Size of the data which follows. */
	uint8_t data_size;
	/** Version of the data. */
	uint8_t data_version;

	/* Followed by data_size bytes of data. */
};

/** Jump data (at end of RAM, or preceding panic data). */
static struct jump_data *jdata;

/**
 * The flags set by the reset cause. These will be a combination of
 * EC_RESET_FLAG_*s and will be used to control the logic of initializing the
 * system image.
 */
static uint32_t reset_flags;

/** Whether or not we've successfully loaded/jumped the current image. */
static bool jumped_to_image;

/* static void jump_to_image */

/* TODO(b/171407461) implement components/panic */
static uintptr_t get_panic_data_start(void)
{
	return 0;
}

void system_common_pre_init(void)
{
	/* TODO check for watchdog reset. */

	/*
	 * In testing we will override the jdata address via
	 * system_override_jdata.
	 */
	if (!IS_ENABLED(CONFIG_ZTEST)) {
		uintptr_t addr = get_panic_data_start();

		if (!addr)
			addr = CONFIG_CROS_EC_RAM_BASE +
			       CONFIG_CROS_EC_RAM_SIZE;

		jdata = (struct jump_data *)(addr - sizeof(struct jump_data));
	}

	/* Check jump data if this is a jump between images. */
	if (jdata->magic == JUMP_DATA_MAGIC && jdata->version >= 1) {
		/*
		 * Change in jump data struct size between the previous image
		 * and this one.
		 */
		int delta;

		/* Set the flag so others know we jumped. */
		jumped_to_image = true;
		/* Restore the reset_flags. */
		reset_flags = jdata->reset_flags | EC_RESET_FLAG_SYSJUMP;

		/*
		 * If the jump data structure isn't the same size as the
		 * current one, shift the jump tags to immediately before the
		 * current jump data structure, to make room for initializing
		 * the new fields below.
		 */
		delta = sizeof(struct jump_data) - jdata->struct_size;

		if (delta && jdata->jump_tag_total) {
			uint8_t *d = (uint8_t *)system_usable_ram_end();

			memmove(d, d + delta, jdata->jump_tag_total);
		}

		jdata->jump_tag_total = 0;

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
		memset(jdata, 0, sizeof(struct jump_data));
	}
}

uintptr_t system_usable_ram_end(void)
{
	return (uintptr_t)jdata - jdata->jump_tag_total;
}

int system_add_jump_tag(uint16_t tag, int version, int size, const void *data)
{
	struct jump_tag *t;

	/* Only allowed during a sysjump. */
	if (!jdata || jdata->magic != JUMP_DATA_MAGIC)
		return -EINVAL;

	/* Make room for the new tag. */
	if (size < 0 || size > 0xff)
		return -EINVAL;
	jdata->jump_tag_total += ROUNDUP4(size) + sizeof(struct jump_tag);

	t = (struct jump_tag *)system_usable_ram_end();
	t->tag = tag;
	t->data_size = size;
	t->data_version = version;
	if (size)
		memcpy(t + 1, data, size);

	return 0;
}

const uint8_t *system_get_jump_tag(uint16_t tag, int *version, int *size)
{
	const struct jump_tag *t;
	int used = 0;

	if (!jdata)
		return NULL;

	/* Search through tag data for a match. */
	while (used < jdata->jump_tag_total) {
		/* Check the next tag. */
		t = (const struct jump_tag *)(system_usable_ram_end() + used);
		used += sizeof(struct jump_tag) + ROUNDUP4(t->data_size);
		if (t->tag != tag)
			continue;

		/* Found a match. */
		if (size)
			*size = t->data_size;
		if (version)
			*version = t->data_version;

		return (const uint8_t *)(t + 1);
	}

	/* If we got here, there was no match. */
	return NULL;
}

void system_encode_save_flags(int reset_flags, uint32_t *save_flags)
{
	*save_flags = 0;

	/* Save current reset reasons if necessary. */
	if (reset_flags & SYSTEM_RESET_PRESERVE_FLAGS)
		*save_flags = system_get_reset_flags() |
			      EC_RESET_FLAG_PRESERVED;

	/* Add in AP off flag into saved flags. */
	if (reset_flags & SYSTEM_RESET_LEAVE_AP_OFF)
		*save_flags |= EC_RESET_FLAG_AP_OFF;

	/* Add in stay in RO flag into saved flags. */
	if (reset_flags & SYSTEM_RESET_STAY_IN_RO)
		*save_flags |= EC_RESET_FLAG_STAY_IN_RO;

	/* Save reset flag. */
	if (reset_flags & (SYSTEM_RESET_HARD | SYSTEM_RESET_WAIT_EXT))
		*save_flags |= EC_RESET_FLAG_HARD;
	else
		*save_flags |= EC_RESET_FLAG_SOFT;
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

int system_jumped_to_this_image(void)
{
	return jumped_to_image;
}

void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	/*
	 * TODO(b:173787365): implement this.  For now, doing nothing
	 * won't break anything, just will eat power.
	 */
}

__test_only void system_common_reset_state(void)
{
	jdata = 0;
	reset_flags = 0;
	jumped_to_image = false;
}

__test_only void system_override_jdata(void *test_jdata)
{
	jdata = (struct jump_data *)test_jdata;
}
