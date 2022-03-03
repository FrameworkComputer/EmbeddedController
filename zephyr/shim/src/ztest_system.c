/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "cros_version.h"
#include "battery.h"
#include "charge_manager.h"
#include "sysjump.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

struct jump_data mock_jump_data = {};

/* When CONFIG_RAM_SIZE is defined, this is provided by common/system.c */
#ifndef CONFIG_RAM_SIZE
struct jump_data *get_jump_data(void)
{
	return &mock_jump_data;
}
#endif

__attribute__((weak)) void system_reset(int flags)
{
	__builtin_unreachable();
}

static uint8_t bbram[SYSTEM_BBRAM_IDX_TRY_SLOT + 1];

test_mockable int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	if (idx <= SYSTEM_BBRAM_IDX_TRY_SLOT) {
		*value = bbram[idx];
		return EC_SUCCESS;
	}
	return EC_ERROR_INVAL;
}

test_mockable int system_set_bbram(enum system_bbram_idx idx, uint8_t value)
{
	if (idx <= SYSTEM_BBRAM_IDX_TRY_SLOT) {
		bbram[idx] = value;
		return EC_SUCCESS;
	}
	return EC_ERROR_INVAL;
}

/* When CONFIG_EC_PROTECTED_STORAGE_OFF is defined, this is provided by
 * common/system.c
 */
#ifndef CONFIG_EC_PROTECTED_STORAGE_OFF
uint32_t flash_get_rw_offset(enum ec_image copy)
{
	return 0;
}
#endif

/* When CONFIG_PLATFORM_EC_FLASH_CROS is defined, this is provided by
 * common/flash.c
 */
#ifndef CONFIG_PLATFORM_EC_FLASH_CROS
int crec_flash_read(int offset, int size, char *data)
{
	return EC_ERROR_INVAL;
}

uint32_t crec_flash_get_protect(void)
{
	return 0;
}
#endif

test_mockable const char *system_get_chip_vendor(void)
{
	return "Testing";
}

test_mockable const char *system_get_chip_name(void)
{
	return "emu";
}

test_mockable const char *system_get_chip_revision(void)
{
	return "";
}

void board_reset_pd_mcu(void)
{
}

test_mockable void system_hibernate(uint32_t seconds, uint32_t microseconds)
{
}
