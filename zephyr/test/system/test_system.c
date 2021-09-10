/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <drivers/bbram.h>
#include <logging/log.h>
#include <ztest.h>

#include "bbram.h"
#include "system.h"

LOG_MODULE_REGISTER(test);

#define BBRAM_REGION_OFF(name) \
	DT_PROP(DT_PATH(named_bbram_regions, name), offset)
#define BBRAM_REGION_SIZE(name) \
	DT_PROP(DT_PATH(named_bbram_regions, name), size)

static char mock_data[64] =
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@";

static int mock_bbram_read(const struct device *unused, size_t offset,
			   size_t size, uint8_t *data)
{
	if (offset < 0 || offset + size >= ARRAY_SIZE(mock_data))
		return -1;
	memcpy(data, mock_data + offset, size);
	return EC_SUCCESS;
}

static const struct bbram_driver_api bbram_api = {
	.read = mock_bbram_read,
};

static const struct device bbram_dev_instance = {
	.name = "TEST_BBRAM_DEV",
	.config = NULL,
	.api = &bbram_api,
	.data = NULL,
};

const struct device *bbram_dev = &bbram_dev_instance;

static void test_bbram_get(void)
{
	uint8_t output[10];
	int rc;

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD0, output);
	zassert_equal(rc, 0, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(pd0),
			  BBRAM_REGION_SIZE(pd0), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD1, output);
	zassert_equal(rc, 0, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(pd1),
			  BBRAM_REGION_SIZE(pd1), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_PD2, output);
	zassert_equal(rc, 0, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(pd2),
			  BBRAM_REGION_SIZE(pd2), NULL);

	rc = system_get_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT, output);
	zassert_equal(rc, 0, NULL);
	zassert_mem_equal(output, mock_data + BBRAM_REGION_OFF(try_slot),
			  BBRAM_REGION_SIZE(try_slot), NULL);
}

void test_main(void)
{
	ztest_test_suite(system, ztest_unit_test(test_bbram_get));
	ztest_run_test_suite(system);
}
