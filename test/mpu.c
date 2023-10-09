/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This test file meant to be executed on a real device. Example:
 * 1. make tests BOARD=bloonchipper
 * 2. servod --board=bloonchipper
 * 3. flash_ec --board bloonchipper --image build/bloonchipper/test-mpu.bin
 * 4. Open console via dut-control raw_fpmcu_console_uart_pty
 * 5. runtest on console
 */

#include "mpu.h"
#include "mpu_private.h"
#include "string.h"
#include "system.h"
#include "test_util.h"

#include <stdbool.h>

struct mpu_info {
	bool has_mpu;
	int num_mpu_regions;
	bool mpu_is_unified;
};

#if defined(CHIP_VARIANT_STM32F412)
struct mpu_info mpu_info = { .has_mpu = true,
			     .num_mpu_regions = 8,
			     .mpu_is_unified = true };

struct mpu_rw_regions expected_rw_regions = { .num_regions = 2,
					      .addr = { 0x08060000,
							0x08080000 },
					      .size = { 0x20000, 0x80000 } };
#elif defined(CHIP_VARIANT_STM32H7X3)
struct mpu_info mpu_info = { .has_mpu = true,
			     .num_mpu_regions = 16,
			     .mpu_is_unified = true };

struct mpu_rw_regions expected_rw_regions = { .num_regions = 1,
					      .addr = { 0x08100000,
							0x08200000 },
					      .size = { 0x100000, 0 } };
#elif defined(CHIP_VARIANT_NPCX9M8S)
struct mpu_info mpu_info = { .has_mpu = true,
			     .num_mpu_regions = 8,
			     .mpu_is_unified = true };

/* unnecessary since NPCX9M8S uses CONFIG_EXTERNAL_STORAGE */
struct mpu_rw_regions expected_rw_regions = { .num_regions = 0,
					      .addr = {},
					      .size = {} };
#else
#error "MPU info not defined for this chip. Please add it."
#endif

test_static int test_mpu_info(void)
{
	TEST_EQ(mpu_num_regions(), mpu_info.num_mpu_regions, "%d");
	TEST_EQ(has_mpu(), mpu_info.has_mpu, "%d");
	TEST_EQ(mpu_is_unified(), mpu_info.mpu_is_unified, "%d");
	return EC_SUCCESS;
}

test_static int reset_mpu(void)
{
	int i;

	mpu_disable();

	for (i = 0; i < mpu_info.num_mpu_regions; ++i) {
		/*
		 * Disable all regions.
		 *
		 * We use the smallest possible size (32 bytes), but it
		 * doesn't really matter since the regions are disabled.
		 */
		TEST_EQ(mpu_config_region(i, 0, 32, 0, 0), EC_SUCCESS, "%d");
	}

	mpu_enable();

	return EC_SUCCESS;
}

test_static int test_mpu_update_region_valid_region(void)
{
	volatile char data __maybe_unused;

	char *const ram_base = (char *const)CONFIG_RAM_BASE;
	const uint8_t size_bit = 5;
	uint16_t mpu_attr = MPU_ATTR_NO_NO;

	/*
	 * Initial read should work. MPU is not protecting the given address.
	 */
	data = ram_base[0];

	TEST_EQ(mpu_update_region(0, (uint32_t)ram_base, size_bit, mpu_attr, 1,
				  0),
		EC_SUCCESS, "%d");

	/* This panics with a data violation at CONFIG_RAM_BASE:
	 *
	 * Data access violation, mfar = <CONFIG_RAM_BASE>
	 */
	data = ram_base[0];

	return EC_SUCCESS;
}

test_static int test_mpu_update_region_invalid_region(void)
{
	/* Test invalid region */
	TEST_EQ(mpu_update_region(mpu_info.num_mpu_regions, 0x8020000, 17,
				  0x1000, 1, 0),
		-EC_ERROR_INVAL, "%d");
	return EC_SUCCESS;
}

test_static int test_mpu_update_region_invalid_alignment(void)
{
	/*
	 * Test size that is not aligned to address.
	 */
	const uint32_t addr = 0x20000;
	const uint32_t size = 0x40000;
	const uint32_t size_bit = 18;

	TEST_EQ(size, BIT(size_bit), "%d");
	TEST_EQ(reset_mpu(), EC_SUCCESS, "%d");
	TEST_EQ(mpu_update_region(0, addr, size_bit, 0, 1, 0), -EC_ERROR_INVAL,
		"%d");

	return EC_SUCCESS;
}

test_static int test_mpu_lock_ro_flash(void)
{
	if (!IS_ENABLED(CONFIG_EXTERNAL_STORAGE) &&
	    IS_ENABLED(CONFIG_FLASH_PHYSICAL)) {
		int rv;

		rv = mpu_lock_ro_flash();
		TEST_EQ(rv, EC_SUCCESS, "%d");
	}

	return EC_SUCCESS;
}

test_static int test_mpu_lock_rw_flash(void)
{
	if (!IS_ENABLED(CONFIG_EXTERNAL_STORAGE) &&
	    IS_ENABLED(CONFIG_FLASH_PHYSICAL)) {
		int rv;

		rv = mpu_lock_rw_flash();
		TEST_EQ(rv, EC_SUCCESS, "%d");
	}

	return EC_SUCCESS;
}

test_static int test_mpu_protect_data_ram(void)
{
	int rv;

	rv = mpu_protect_data_ram();
	TEST_EQ(rv, EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

test_static int test_mpu_protect_code_ram(void)
{
	if (IS_ENABLED(CONFIG_EXTERNAL_STORAGE) ||
	    !IS_ENABLED(CONFIG_FLASH_PHYSICAL)) {
		int rv;

		rv = mpu_protect_code_ram();
		TEST_EQ(rv, EC_SUCCESS, "%d");
	}

	return EC_SUCCESS;
}

test_static int test_mpu_get_rw_regions(void)
{
	if (!IS_ENABLED(CONFIG_EXTERNAL_STORAGE) &&
	    IS_ENABLED(CONFIG_FLASH_PHYSICAL)) {
		struct mpu_rw_regions rw_regions = mpu_get_rw_regions();
		int rv = memcmp(&rw_regions, &expected_rw_regions,
				sizeof(expected_rw_regions));

		TEST_EQ(rv, 0, "%d");
	}
	return EC_SUCCESS;
}

test_static int test_align_down_to_bits(void)
{
	uint32_t addr = 0x87654321;

	TEST_EQ(align_down_to_bits(addr, 0), addr, "%d");
	TEST_EQ(align_down_to_bits(addr, 1), 0x87654320, "%d");
	TEST_EQ(align_down_to_bits(addr, 30), 0x80000000, "%d");
	TEST_EQ(align_down_to_bits(addr, 31), 0x80000000, "%d");
	TEST_EQ(align_down_to_bits(addr, 32), addr, "%d");
	TEST_EQ(align_down_to_bits(addr, 33), addr, "%d");
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	enum ec_image cur_image = system_get_image_copy();

	ccprintf("Running MPU test\n");

	RUN_TEST(reset_mpu);
	RUN_TEST(test_mpu_info);

	/*
	 * TODO(b/151105339): For all locked regions, check that we cannot
	 * read/write/execute (depending on the configuration).
	 */

	/*
	 * Since locking prevents code execution, we can only lock the region
	 * that is not running or the test will hang.
	 */
	if (cur_image == EC_IMAGE_RW) {
		RUN_TEST(reset_mpu);
		RUN_TEST(test_mpu_lock_ro_flash);
	}

	if (cur_image == EC_IMAGE_RO) {
		RUN_TEST(reset_mpu);
		RUN_TEST(test_mpu_lock_rw_flash);
	}

	RUN_TEST(reset_mpu);
	RUN_TEST(test_mpu_update_region_invalid_region);
	RUN_TEST(reset_mpu);
	RUN_TEST(test_mpu_update_region_invalid_alignment);
	RUN_TEST(reset_mpu);
	RUN_TEST(test_mpu_protect_code_ram);
	RUN_TEST(reset_mpu);
	RUN_TEST(test_mpu_protect_data_ram);
	RUN_TEST(reset_mpu);
	RUN_TEST(test_mpu_get_rw_regions);
	RUN_TEST(reset_mpu);
	RUN_TEST(test_align_down_to_bits);
	RUN_TEST(reset_mpu);
	/* This test must be last because it generates a panic */
	RUN_TEST(test_mpu_update_region_valid_region);
	RUN_TEST(reset_mpu);
	test_print_result();
}
