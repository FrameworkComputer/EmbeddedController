/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash module for emulator */

#include <stdio.h>

#include "common.h"
#include "flash.h"
#include "persistence.h"
#include "util.h"

char __host_flash[CONFIG_FLASH_PHYSICAL_SIZE];
uint8_t __host_flash_protect[PHYSICAL_BANKS];

static int flash_check_protect(int offset, int size)
{
	int first_bank = offset / CONFIG_FLASH_BANK_SIZE;
	int last_bank = DIV_ROUND_UP(offset + size + 1,
				     CONFIG_FLASH_BANK_SIZE);
	int bank;

	for (bank = first_bank; bank <= last_bank; ++bank)
		if (__host_flash_protect[bank])
			return 1;
	return 0;
}

static void flash_set_persistent(void)
{
	FILE *f = get_persistent_storage("flash", "wb");
	int sz;

	ASSERT(f != NULL);

	sz = fwrite(__host_flash, sizeof(__host_flash), 1, f);
	ASSERT(sz == 1);

	release_persistent_storage(f);
}

static void flash_get_persistent(void)
{
	FILE *f = get_persistent_storage("flash", "rb");

	if (f == NULL) {
		fprintf(stderr,
			"No flash storage found. Initializing to 0xff.\n");
		memset(__host_flash, 0xff, sizeof(__host_flash));
		return;
	}

	fread(__host_flash, sizeof(__host_flash), 1, f);

	release_persistent_storage(f);
}

int flash_physical_write(int offset, int size, const char *data)
{
	ASSERT((size & (CONFIG_FLASH_WRITE_SIZE - 1)) == 0);

	if (flash_check_protect(offset, size))
		return EC_ERROR_ACCESS_DENIED;

	memcpy(__host_flash + offset, data, size);
	flash_set_persistent();

	return EC_SUCCESS;
}

int flash_physical_erase(int offset, int size)
{
	ASSERT((size & (CONFIG_FLASH_ERASE_SIZE - 1)) == 0);

	if (flash_check_protect(offset, size))
		return EC_ERROR_ACCESS_DENIED;

	memset(__host_flash + offset, 0xff, size);
	flash_set_persistent();

	return EC_SUCCESS;
}

int flash_physical_get_protect(int bank)
{
	return __host_flash_protect[bank];
}

uint32_t flash_physical_get_protect_flags(void)
{
	int i;
	uint32_t flags = EC_FLASH_PROTECT_ALL_NOW;

	for (i = 0; i < PHYSICAL_BANKS; ++i)
		if (__host_flash_protect[i] == 0)
			flags = 0;

	return flags;
}

int flash_physical_protect_now(int all)
{
	memset(__host_flash_protect, 1, all ? PHYSICAL_BANKS : RO_BANK_COUNT);
	return EC_SUCCESS;
}

int flash_pre_init(void)
{
	uint32_t prot_flags;

	flash_get_persistent();

	prot_flags = flash_get_protect();

	if (prot_flags & EC_FLASH_PROTECT_GPIO_ASSERTED) {
		/*
		 * Write protect is asserted.  If we want RO flash protected,
		 * protect it now.
		 */
		if ((prot_flags & EC_FLASH_PROTECT_RO_AT_BOOT) &&
		    !(prot_flags & EC_FLASH_PROTECT_RO_NOW)) {
			int rv = flash_set_protect(EC_FLASH_PROTECT_RO_NOW,
						   EC_FLASH_PROTECT_RO_NOW);
			if (rv)
				return rv;

			/* Re-read flags */
			prot_flags = flash_get_protect();
		}
	}

	return EC_SUCCESS;
}
