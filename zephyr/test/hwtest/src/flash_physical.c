/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(flash_physical, NULL, NULL, NULL, NULL, NULL);

struct flash_info {
	int num_flash_banks;
	int write_protect_bank_offset;
	int write_protect_bank_count;
};

#if defined(CONFIG_SOC_FAMILY_STM32)
/* Covered by Zephyr tests. "flash_stm32" suite for STM32 chips */
ZTEST(flash_physical, test_flash_physical)
{
	zassert_equal(1, 1);
}
#elif defined(CONFIG_SOC_FAMILY_NPCX)

struct flash_info flash_info = {
	.num_flash_banks = 16,
	.write_protect_bank_offset = 0,
	/* Helipilot's CONFIG_RO_SIZE is 128 KB and write protect size is 64 KB,
	 * so there should be 2 banks
	 */
	.write_protect_bank_count = 2,
};

ZTEST(flash_physical, test_flash_config)
{
	zassert_equal(crec_flash_total_banks(), flash_info.num_flash_banks,
		      "Flash total banks mismatch");

	zassert_equal(WP_BANK_OFFSET, flash_info.write_protect_bank_offset,
		      "WP bank offset mismatch");

	zassert_equal(WP_BANK_COUNT, flash_info.write_protect_bank_count,
		      "WP bank count mismatch");
}
#else
#error "Flash tests not defined for this chip. Please add it."
#endif
