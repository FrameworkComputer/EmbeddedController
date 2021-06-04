/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Init ROM module for Chrome EC */

#include "builtin/assert.h"
#include "common.h"
#include "init_rom.h"
#include "flash.h"
#include "stdbool.h"
#include "stddef.h"

const void *init_rom_map(const void *addr, int size)
{
	const char *src;
	uintptr_t offset;

	/*
	 * When CONFIG_CHIP_INIT_ROM_REGION isn't enabled, .init_rom objects
	 * are linked into the .rodata section and directly addressable.
	 * Return the caller's pointer.
	 */
	if (!IS_ENABLED(CONFIG_CHIP_INIT_ROM_REGION))
		return addr;

	/*
	 * When flash isn't memory mapped, caller's must use init_rom_copy()
	 * to copy .init_rom data into RAM.
	 */
	if (!IS_ENABLED(CONFIG_MAPPED_STORAGE))
		return NULL;

	/*
	 * Safe pointer conversion - needed for host tests which can have
	 * 64-bit pointers.
	 */
	offset = (uintptr_t)addr;

	ASSERT(offset <= __INT_MAX__);

	/*
	 * Convert flash offset to memory mapped address
	 */
	if (crec_flash_dataptr((int)offset, size, 1, &src) < 0)
		return NULL;

	/* Once the flash offset is validated, lock the flash for the caller */
	crec_flash_lock_mapped_storage(1);

	return src;
}

/*
 * The addr and size parameters are provided for forward compatibility if
 * the flash API is extended to support locking less than the entire flash.
 */
void init_rom_unmap(const void *addr, int size)
{
	if (IS_ENABLED(CONFIG_CHIP_INIT_ROM_REGION))
		crec_flash_lock_mapped_storage(0);
}

int init_rom_copy(int offset, int size, char *data)
{
	return crec_flash_read(offset, size, data);
}

