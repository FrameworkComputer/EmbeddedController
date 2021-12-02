/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <drivers/bbram.h>
#include <logging/log.h>
#include <ztest.h>

#include "system.h"

LOG_MODULE_REGISTER(test);

#define BBRAM_REGION_OFF(name) \
	DT_PROP(DT_PATH(named_bbram_regions, name), offset)
#define BBRAM_REGION_SIZE(name) \
	DT_PROP(DT_PATH(named_bbram_regions, name), size)

static char mock_data[64] =
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@";

static void test_bbram_get(void)
{
	const struct device *const bbram_dev =
		DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram));
	uint8_t output[10];
	int rc;

	/* Write expected data to read back */
	rc = bbram_write(bbram_dev, 0, ARRAY_SIZE(mock_data), mock_data);
	zassert_ok(rc, NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD0, output);
	zassert_ok(rc, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(pd0),
			  BBRAM_REGION_SIZE(pd0), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD1, output);
	zassert_ok(rc, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(pd1),
			  BBRAM_REGION_SIZE(pd1), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD2, output);
	zassert_ok(rc, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(pd2),
			  BBRAM_REGION_SIZE(pd2), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT, output);
	zassert_ok(rc, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(try_slot),
			  BBRAM_REGION_SIZE(try_slot), NULL);
}

void test_main(void)
{
	ztest_test_suite(system, ztest_unit_test(test_bbram_get));
	ztest_run_test_suite(system);
}
