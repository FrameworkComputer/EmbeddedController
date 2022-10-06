/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash module for emulator */

#include <stdio.h>

#include "builtin/assert.h"
#include "common.h"
#include "config_chip.h"
#include "flash.h"
#include "persistence.h"
#include "util.h"

/* This needs to be aligned to the erase bank size for NVCTR. */
__aligned(CONFIG_FLASH_ERASE_SIZE) char __host_flash[CONFIG_FLASH_SIZE_BYTES];
uint8_t __host_flash_protect[PHYSICAL_BANKS];

/* Override this function to make flash erase/write operation fail */
test_mockable int flash_pre_op(void)
{
	return EC_SUCCESS;
}

static int flash_check_protect(int offset, int size)
{
	int first_bank = offset / CONFIG_FLASH_BANK_SIZE;
	int last_bank = DIV_ROUND_UP(offset + size, CONFIG_FLASH_BANK_SIZE);
	int bank;

	for (bank = first_bank; bank < last_bank; ++bank)
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
	int sz;

	if (f == NULL) {
		fprintf(stderr,
			"No flash storage found. Initializing to 0xff.\n");
		memset(__host_flash, 0xff, sizeof(__host_flash));
		return;
	}

	sz = fread(__host_flash, sizeof(__host_flash), 1, f);
	ASSERT(sz == 1);

	release_persistent_storage(f);
}

int crec_flash_physical_write(int offset, int size, const char *data)
{
	ASSERT((size & (CONFIG_FLASH_WRITE_SIZE - 1)) == 0);

	if (flash_pre_op() != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	if (flash_check_protect(offset, size))
		return EC_ERROR_ACCESS_DENIED;

	memcpy(__host_flash + offset, data, size);
	flash_set_persistent();

	return EC_SUCCESS;
}

int crec_flash_physical_erase(int offset, int size)
{
	ASSERT((size & (CONFIG_FLASH_ERASE_SIZE - 1)) == 0);

	if (flash_pre_op() != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	if (flash_check_protect(offset, size))
		return EC_ERROR_ACCESS_DENIED;

	memset(__host_flash + offset, 0xff, size);
	flash_set_persistent();

	return EC_SUCCESS;
}

int crec_flash_physical_get_protect(int bank)
{
	return __host_flash_protect[bank];
}

uint32_t crec_flash_physical_get_protect_flags(void)
{
	int i;
	uint32_t flags = EC_FLASH_PROTECT_ALL_NOW;

	for (i = 0; i < PHYSICAL_BANKS; ++i)
		if (__host_flash_protect[i] == 0)
			flags = 0;

	return flags;
}

int crec_flash_physical_protect_now(int all)
{
	memset(__host_flash_protect, 1, all ? PHYSICAL_BANKS : WP_BANK_COUNT);
	return EC_SUCCESS;
}

uint32_t crec_flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW |
	       EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t crec_flash_physical_get_writable_flags(uint32_t cur_flags)
{
	uint32_t ret = 0;

	/* If RO protection isn't enabled, its at-boot state can be changed. */
	if (!(cur_flags & EC_FLASH_PROTECT_RO_NOW))
		ret |= EC_FLASH_PROTECT_RO_AT_BOOT;

	/*
	 * If entire flash isn't protected at this boot, it can be enabled if
	 * the WP GPIO is asserted.
	 */
	if (!(cur_flags & EC_FLASH_PROTECT_ALL_NOW) &&
	    (cur_flags & EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ALL_NOW;

	return ret;
}

int crec_flash_pre_init(void)
{
	uint32_t prot_flags;

	flash_get_persistent();

	prot_flags = crec_flash_get_protect();

	if (prot_flags & EC_FLASH_PROTECT_GPIO_ASSERTED) {
		/*
		 * Write protect is asserted.  If we want RO flash protected,
		 * protect it now.
		 */
		if ((prot_flags & EC_FLASH_PROTECT_RO_AT_BOOT) &&
		    !(prot_flags & EC_FLASH_PROTECT_RO_NOW)) {
			int rv =
				crec_flash_set_protect(EC_FLASH_PROTECT_RO_NOW,
						       EC_FLASH_PROTECT_RO_NOW);
			if (rv)
				return rv;

			/* Re-read flags */
			prot_flags = crec_flash_get_protect();
		}
	}

	return EC_SUCCESS;
}
