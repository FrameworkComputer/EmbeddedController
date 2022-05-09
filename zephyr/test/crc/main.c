/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <ztest.h>

#include "crc8.h"

/* Note this test makes the pure platform/ec test that uses the same value */
static void test_crc8_known_data(void)
{
	uint8_t buffer[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 8 };

	int crc = cros_crc8(buffer, 10);

	/* Verifies polynomial values of 0x07 representing x^8 + x^2 + x + 1 */
	zassert_equal(crc, 170, "CRC8 hash did not match");
}

void test_main(void)
{
	ztest_test_suite(test_task_shim,
			 ztest_unit_test(test_crc8_known_data));
	ztest_run_test_suite(test_task_shim);
}
