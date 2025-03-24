/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/pdc_power_mgmt.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/ztest.h>

/** Enable port 1, but not port 0. This discontiguous layout
 *  is not allowed. No ports will be enabled in this case.
 */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	switch (port) {
	case 0:
		*dev = NULL;
		return 0;
	case 1:
		*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_emul2));
		return 0;
	}

	*dev = NULL;
	return -ERANGE;
}

ZTEST_SUITE(pdc_runtime_config_discontiguous, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(pdc_runtime_config_discontiguous, test_board_count)
{
	zassert_equal(0, pdc_power_mgmt_get_usb_pd_port_count());
}

ZTEST_USER(pdc_runtime_config_discontiguous, test_board_get_drivers)
{
	zassert_is_null(pdc_power_mgmt_get_port_pdc_driver(0));
	zassert_is_null(pdc_power_mgmt_get_port_pdc_driver(1));
}

ZTEST_USER(pdc_runtime_config_discontiguous, test_board_get_state)
{
	/* Ports could have been active, but are disabled */
	zassert_equal(PDC_DISABLED, pdc_power_mgmt_get_task_state(0));
	zassert_equal(PDC_DISABLED, pdc_power_mgmt_get_task_state(1));

	/* Non-existent ports beyond the board's max port count */
	zassert_equal(PDC_INVALID, pdc_power_mgmt_get_task_state(2));
	zassert_equal(PDC_INVALID, pdc_power_mgmt_get_task_state(3));
}
