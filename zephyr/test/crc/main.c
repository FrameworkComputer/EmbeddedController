/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test_new.h>

#include "crc8.h"

ZTEST_SUITE(crc_driver, NULL, NULL, NULL, NULL, NULL);

ZTEST(crc_driver, test_crc8_known_data)
{
	uint8_t buffer[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 8 };

	int crc = cros_crc8(buffer, 10);

	/* Verifies polynomial values of 0x07 representing x^8 + x^2 + x + 1 */
	zassert_equal(crc, 170, "CRC8 hash did not match");
}
