/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"

#include <zephyr/ztest.h>

ZTEST_USER(flash, test_bank_size)
{
	for (int i = 0; i < crec_flash_total_banks(); i++) {
		zassert_equal(crec_flash_bank_size(i), CONFIG_FLASH_BANK_SIZE,
			      "crec_flash_bank_size(%d) = %d", i,
			      crec_flash_bank_size(i));
	}
}

ZTEST_USER(flash, test_bank_erase_size)
{
	for (int i = 0; i < crec_flash_total_banks(); i++) {
		zassert_equal(crec_flash_bank_erase_size(i),
			      CONFIG_FLASH_ERASE_SIZE,
			      "crec_flash_bank_erase_size(%d) = %d", i,
			      crec_flash_bank_erase_size(i));
	}
}

ZTEST_USER(flash, test_bank_start_offset)
{
	for (int i = 0; i < crec_flash_total_banks(); i++) {
		zassert_equal(crec_flash_bank_start_offset(i),
			      CONFIG_FLASH_BANK_SIZE * i,
			      "crec_flash_bank_start_offset(%d) = %d", i,
			      crec_flash_bank_start_offset(i));
	}
}

ZTEST_USER(flash, test_bank_size_invalid)
{
	int invalid_bank = crec_flash_total_banks() + 1;

	zassert_equal(crec_flash_bank_size(invalid_bank), -1, NULL);
}

ZTEST_USER(flash, test_bank_erase_size_invalid)
{
	int invalid_bank = crec_flash_total_banks() + 1;

	zassert_equal(crec_flash_bank_erase_size(invalid_bank), -1, NULL);
}

ZTEST_USER(flash, test_bank_start_offset_invalid)
{
	int invalid_bank = crec_flash_total_banks() + 1;

	zassert_equal(crec_flash_bank_start_offset(invalid_bank), -1, NULL);
}

ZTEST_USER(flash, test_bank_index_invalid)
{
	int invalid_offset = 2 * CONFIG_FLASH_SIZE_BYTES;

	zassert_equal(crec_flash_bank_index(invalid_offset), -1, NULL);
}

ZTEST_USER(flash, test_bank_count)
{
	zassert_equal(crec_flash_bank_count(0, 1), 1, NULL);
	zassert_equal(crec_flash_bank_count(0, CONFIG_FLASH_BANK_SIZE), 1,
		      NULL);
	zassert_equal(crec_flash_bank_count(0, CONFIG_FLASH_BANK_SIZE + 1), 2,
		      NULL);
	zassert_equal(crec_flash_bank_count(1, CONFIG_FLASH_BANK_SIZE), 2,
		      NULL);
	zassert_equal(crec_flash_bank_count(CONFIG_FLASH_BANK_SIZE - 1, 2), 2,
		      NULL);
	zassert_equal(crec_flash_bank_count(0, CONFIG_FLASH_SIZE_BYTES),
		      crec_flash_total_banks(), NULL);
}

ZTEST_USER(flash, test_bank_count_invalid)
{
	zassert_equal(crec_flash_bank_count(0, 0), -1, NULL);
	zassert_equal(crec_flash_bank_count(CONFIG_FLASH_SIZE_BYTES + 1, 0), -1,
		      NULL);
	zassert_equal(crec_flash_bank_count(0, CONFIG_FLASH_SIZE_BYTES + 1), -1,
		      NULL);
	zassert_equal(crec_flash_bank_count(1, CONFIG_FLASH_SIZE_BYTES), -1,
		      NULL);
}

ZTEST_USER(flash, test_offset_to_sector_conversion)
{
	int offset = 123456;
	int sector = crec_flash_bank_index(offset);
	int sector_offset = crec_flash_bank_start_offset(sector);
	int sector_size = crec_flash_bank_size(sector);

	zassert_between_inclusive(offset, sector_offset,
				  sector_offset + sector_size, NULL);
}
