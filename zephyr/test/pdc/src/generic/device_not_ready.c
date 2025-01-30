/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/pdc_power_mgmt.h"

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(pdc_device_not_ready, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(pdc_device_not_ready, test_pdc_device_not_ready)
{
	/* Expect that PDC subsys port 0 is functional. */
	zassert_ok(pdc_power_mgmt_wait_for_sync(0, -1));
	zassert_equal(pdc_power_mgmt_get_task_state(0), PDC_UNATTACHED);

	/* PDC emul on port 1 is marked as deferred-init, So the PDC driver
	 * and pdc_power_mgmt driver should be in the disabled state.
	 */
	zassert_equal(pdc_power_mgmt_wait_for_sync(1, -1), -ERANGE);
	zassert_equal(pdc_power_mgmt_get_task_state(1), PDC_DISABLED);
}
