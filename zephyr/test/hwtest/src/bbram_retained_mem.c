/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/retained_mem.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(bbram_retained_mem, NULL, NULL, NULL, NULL, NULL);

ZTEST(bbram_retained_mem, test_retained_mem_read_write)
{
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram));
	uint8_t data[] = { 0x1, 0x2, 0x3, 0x4 };
	uint8_t buffer[sizeof(data)];
	int rc;

	zassert_true(device_is_ready(dev), "Device not ready");

	rc = retained_mem_write(dev, 0, data, sizeof(data));
	zassert_equal(rc, 0, "Write failed");

	memset(buffer, 0, sizeof(buffer));
	rc = retained_mem_read(dev, 0, buffer, sizeof(buffer));
	zassert_equal(rc, 0, "Read failed");

	zassert_mem_equal(data, buffer, sizeof(data), "Data mismatch");
}
