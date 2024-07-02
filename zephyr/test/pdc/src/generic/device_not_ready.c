/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

void ztest_post_fatal_error_hook(unsigned int reason,
				 const struct arch_esf *pEsf)
{
	/* check if expected error */
	zassert_equal(reason, K_ERR_KERNEL_OOPS);
}

ZTEST_SUITE(pdc_device_not_ready, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(pdc_device_not_ready, test_pdc_device_not_ready)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(usbc0));

	ztest_set_fault_valid(true);
	device_init(dev);
} /* LCOV_EXCL_LINE device_init will crash, so this function will not return. */
