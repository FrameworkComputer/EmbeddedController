/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "mpu.h"

void mpu_enable(void)
{
	/* TODO(b/180039888): Implement this */
}

uint32_t mpu_get_type(void)
{
	/* TODO(b/180039888): Implement this */
	return 0;
}

int mpu_protect_data_ram(void)
{
	/*
	 * TODO(b/180039888): Implement this
	 * Update:
	 *    address	CONFIG_RAM_BASE,
	 *    size	CONFIG_DATA_RAM_SIZE
	 *    attr	No-execute, but allow read/write
	 */

	return 0;
}

#if defined(CONFIG_PLATFORM_EC_EXTERNAL_STORAGE) || \
	!defined(CONFIG_FLASH_PHYSICAL)

int mpu_protect_code_ram(void)
{
	/*
	 * TODO(b/180039888): Implement this
	 * Update:
	 *    address	CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RO_MEM_OFF
	 *    size	CONFIG_CODE_RAM_SIZE
	 *    attr	read-only
	 */

	return 0;
}

#else

int mpu_lock_ro_flash(void)
{
	/* TODO(b/180039888): Implement this
	 * Update:
	 *    address	CONFIG_MAPPED_STORAGE_BASE + CONFIG_RO_MEM_OFF
	 *    size	CONFIG_RO_SIZE
	 *    attr	No-execute, but allow read/write
	 */

	return 0;
}

int mpu_lock_rw_flash(void)
{
	/* TODO(b/180039888): Implement this
	 * Update multiple regions:
	 *    attr	No-execute, but allow read/write
	 */

	return 0;
}

#endif
