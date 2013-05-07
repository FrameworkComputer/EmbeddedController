/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for emulator */

#include <stdlib.h>

#include "host_test.h"
#include "system.h"
#include "persistence.h"

#define SHARED_MEM_SIZE 512 /* bytes */
char __shared_mem_buf[SHARED_MEM_SIZE];

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

void system_pre_init(void)
{
	/* Nothing */
}
