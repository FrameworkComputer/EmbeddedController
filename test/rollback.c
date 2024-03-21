/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "mpu.h"
#include "string.h"
#include "test_util.h"

#include <stdbool.h>

struct rollback_info {
	int region_0_offset;
	int region_1_offset;
	uint32_t region_size_bytes;
};

/* These values are intentionally hardcoded here instead of using the chip
 * config headers, so that if the headers are accidentally changed we can catch
 * it.
 */
#if defined(CHIP_VARIANT_STM32F412)
struct rollback_info rollback_info = {
	.region_0_offset = 0x20000,
	.region_1_offset = 0x40000,
	.region_size_bytes = 128 * 1024,
};
#elif defined(CHIP_VARIANT_STM32H7X3)
struct rollback_info rollback_info = {
	.region_0_offset = 0xC0000,
	.region_1_offset = 0xE0000,
	.region_size_bytes = 128 * 1024,
};
#elif defined(CHIP_VARIANT_NPCX9MFP)
struct rollback_info rollback_info = {
	.region_0_offset = 0x20000,
	.region_1_offset = 0x30000,
	.region_size_bytes = 64 * 1024,
};
#else
#error "Rollback info not defined for this chip. Please add it."
#endif

test_static int read_rollback_region(const struct rollback_info *info,
				     int region)
{
	int i;
	char data;
	uint32_t bytes_read = 0;

	int offset = region == 0 ? info->region_0_offset :
				   info->region_1_offset;

	for (i = 0; i < info->region_size_bytes; i++) {
		if (crec_flash_read(offset + i, sizeof(data), &data) ==
		    EC_SUCCESS)
			bytes_read++;
	}

	return bytes_read;
}

test_static int _test_lock_rollback(const struct rollback_info *info,
				    int region)
{
	int rv;

	/*
	 * We expect the MPU to have already been enabled during the
	 * initialization process (mpu_pre_init).
	 */

	rv = mpu_lock_rollback(0);
	TEST_EQ(rv, EC_SUCCESS, "%d");

	/* unlocked we should be able to read both regions */
	rv = read_rollback_region(info, 0);
	TEST_EQ(rv, rollback_info.region_size_bytes, "%d");

	rv = read_rollback_region(info, 1);
	TEST_EQ(rv, rollback_info.region_size_bytes, "%d");

	rv = mpu_lock_rollback(1);
	TEST_EQ(rv, EC_SUCCESS, "%d");

	/* TODO(b/156112448): Validate that it actually reboots with the correct
	 * data access violation.
	 */
	read_rollback_region(info, region);

	/* Should not get here. Should reboot with:
	 *
	 * Data access violation, mfar = XXX
	 *
	 * where XXX = start of rollback
	 */
	TEST_ASSERT(false);

	return EC_ERROR_UNKNOWN;
}

test_static int test_lock_rollback_region_0(void)
{
	/* This call should never return due to panic. */
	return _test_lock_rollback(&rollback_info, 0);
}

test_static int test_lock_rollback_region_1(void)
{
	/* This call should never return due to panic. */
	return _test_lock_rollback(&rollback_info, 1);
}

void run_test(int argc, const char **argv)
{
	if (argc < 2) {
		ccprintf("usage: runtest [region0|region1]\n");
		return;
	}

	ccprintf("Running rollback test\n");

	/*
	 * TODO(b/156112448): For now you have to run the test separately for
	 * each region.
	 */
	if (strncmp(argv[1], "region0", 7) == 0)
		RUN_TEST(test_lock_rollback_region_0);
	else if (strncmp(argv[1], "region1", 7) == 0)
		RUN_TEST(test_lock_rollback_region_1);

	test_print_result();
}
