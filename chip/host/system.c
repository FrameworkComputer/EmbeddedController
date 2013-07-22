/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for emulator */

#include <stdlib.h>

#include "common.h"
#include "host_test.h"
#include "system.h"
#include "panic.h"
#include "persistence.h"
#include "util.h"

#define SHARED_MEM_SIZE 512 /* bytes */
char __shared_mem_buf[SHARED_MEM_SIZE];

#define RAM_DATA_SIZE (sizeof(struct panic_data) + 512) /* bytes */
static char __ram_data[RAM_DATA_SIZE];

static enum system_image_copy_t __running_copy;

static void ramdata_set_persistent(void)
{
	FILE *f = get_persistent_storage("ramdata", "wb");
	int sz;

	ASSERT(f != NULL);

	sz = fwrite(__ram_data, sizeof(__ram_data), 1, f);
	ASSERT(sz == 1);

	release_persistent_storage(f);
}

static void ramdata_get_persistent(void)
{
	FILE *f = get_persistent_storage("ramdata", "rb");

	if (f == NULL) {
		fprintf(stderr,
			"No RAM data found. Initializing to 0x00.\n");
		memset(__ram_data, 0, sizeof(__ram_data));
		return;
	}

	fread(__ram_data, sizeof(__ram_data), 1, f);

	release_persistent_storage(f);

	/*
	 * Assumes RAM data doesn't preserve across reboot except for sysjump.
	 * Clear persistent data once it's read.
	 */
	remove_persistent_storage("ramdata");
}

static void set_image_copy(uint32_t copy)
{
	FILE *f = get_persistent_storage("image_copy", "wb");

	ASSERT(f != NULL);
	ASSERT(fwrite(&copy, sizeof(copy), 1, f) == 1);

	release_persistent_storage(f);
}

static uint32_t get_image_copy(void)
{
	FILE *f = get_persistent_storage("image_copy", "rb");
	uint32_t ret;

	if (f == NULL)
		return SYSTEM_IMAGE_RO;
	fread(&ret, sizeof(ret), 1, f);
	release_persistent_storage(f);
	remove_persistent_storage("image_copy");

	return ret;
}

test_mockable struct panic_data *panic_get_data(void)
{
	return (struct panic_data *)
		(__ram_data + RAM_DATA_SIZE - sizeof(struct panic_data));
}

test_mockable void system_reset(int flags)
{
	exit(EXIT_CODE_RESET | flags);
}

test_mockable void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
	exit(EXIT_CODE_HIBERNATE);
}

test_mockable int system_is_locked(void)
{
	return 0;
}

test_mockable int system_jumped_to_this_image(void)
{
	return 0;
}

test_mockable uint32_t system_get_reset_flags(void)
{
	return RESET_FLAG_POWER_ON;
}

const char *system_get_chip_vendor(void)
{
	return "chromeos";
}

const char *system_get_chip_name(void)
{
	return "emu";
}

const char *system_get_chip_revision(void)
{
	return "";
}

int system_get_vbnvcontext(uint8_t *block)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	return EC_ERROR_UNIMPLEMENTED;
}

enum system_image_copy_t system_get_image_copy(void)
{
	return __running_copy;
}

int system_set_scratchpad(uint32_t value)
{
	FILE *f = get_persistent_storage("scratchpad", "w");

	fprintf(f, "%lu", value);
	release_persistent_storage(f);

	return EC_SUCCESS;
}

uint32_t system_get_scratchpad(void)
{
	FILE *f = get_persistent_storage("scratchpad", "r");
	uint32_t value;
	int success;

	if (f == NULL)
		return 0;

	success = fscanf(f, "%lu", &value);
	release_persistent_storage(f);

	if (success)
		return value;
	else
		return 0;
}

uintptr_t system_usable_ram_end(void)
{
	return (uintptr_t)(__shared_mem_buf + SHARED_MEM_SIZE);
}

static void __jump_resetvec(void)
{
	ramdata_set_persistent();
	exit(EXIT_CODE_RESET);
}

static void __ro_jump_resetvec(void)
{
	set_image_copy(SYSTEM_IMAGE_RO);
	__jump_resetvec();
}

static void __rw_jump_resetvec(void)
{
	set_image_copy(SYSTEM_IMAGE_RW);
	__jump_resetvec();
}

void system_pre_init(void)
{
	ramdata_get_persistent();
	__running_copy = get_image_copy();

	*(uintptr_t *)(__host_flash + CONFIG_FW_RO_OFF + 4) =
		(uintptr_t)__ro_jump_resetvec;
	*(uintptr_t *)(__host_flash + CONFIG_FW_RW_OFF + 4) =
		(uintptr_t)__rw_jump_resetvec;
}
