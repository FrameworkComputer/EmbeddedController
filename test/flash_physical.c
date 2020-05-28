/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "test_util.h"

struct flash_info {
	int num_flash_banks;
	int write_protect_bank_offset;
	int write_protect_bank_count;
};

#if defined(CHIP_VARIANT_STM32F412)
struct flash_info flash_info = {
	.num_flash_banks = 12,
	.write_protect_bank_offset = 0,
	.write_protect_bank_count = 5,
};
#elif defined(CHIP_VARIANT_STM32H7X3)
struct flash_info flash_info = {
	.num_flash_banks = 16,
	.write_protect_bank_offset = 0,
	.write_protect_bank_count = 6,
};
#else
#error "Flash info not defined for this chip. Please add it."
#endif

test_static int test_flash_config(void)
{
	TEST_EQ(PHYSICAL_BANKS, flash_info.num_flash_banks, "%d");
	TEST_EQ(WP_BANK_OFFSET, flash_info.write_protect_bank_offset, "%d");
	TEST_EQ(WP_BANK_COUNT, flash_info.write_protect_bank_count, "%d");
	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	ccprintf("Running flash physical test\n");
	RUN_TEST(test_flash_config);
	test_print_result();
}
