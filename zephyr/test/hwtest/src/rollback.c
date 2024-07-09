/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "mpu.h"

#include <stdio.h>

#include <zephyr/ztest.h>

struct rollback_info {
	int region_0_offset;
	int region_1_offset;
	uint32_t region_size_bytes;
};

/* These values are intentionally hardcoded here instead of using the chip
 * config headers, so that if the headers are accidentally changed we can catch
 * it.
 */
#if defined(CONFIG_SOC_STM32F412CX)
struct rollback_info rollback_info = {
	.region_0_offset = 0x20000,
	.region_1_offset = 0x40000,
	.region_size_bytes = 128 * 1024,
};
#else
#error "Rollback info not defined for this chip. Please add it."
#endif

static int read_rollback_region(const struct rollback_info *info, int region)
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

static void test_lock_rollback(const struct rollback_info *info, int region)
{
	int rv;

	/*
	 * We expect the MPU to have already been enabled during the
	 * initialization process (mpu_pre_init).
	 */
	rv = mpu_lock_rollback(0);
	zassert_equal(rv, EC_SUCCESS);

	/* unlocked we should be able to read both regions */
	rv = read_rollback_region(info, 0);
	zassert_equal(rv, rollback_info.region_size_bytes);

	rv = read_rollback_region(info, 1);
	zassert_equal(rv, rollback_info.region_size_bytes);

	rv = mpu_lock_rollback(1);
	zassert_equal(rv, EC_SUCCESS);

	read_rollback_region(info, region);

	/* Should not get here. Should reboot with:
	 *
	 * MMFAR Address: XXX
	 *
	 * where XXX = start of rollback
	 */
	zassert_unreachable();
}

ZTEST_SUITE(rollback_region0, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(rollback_region1, NULL, NULL, NULL, NULL, NULL);

ZTEST(rollback_region0, test_rollback_region0)
{
	test_lock_rollback(&rollback_info, 0);
}

ZTEST(rollback_region1, test_rollback_region1)
{
	test_lock_rollback(&rollback_info, 1);
}
