/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/pdc_power_mgmt.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/ztest.h>

/** Create an error while reading the board's port configuration */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	*dev = NULL;
	return -EINVAL;
}

ZTEST_SUITE(pdc_runtime_config_no_pdcs, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(pdc_runtime_config_no_pdcs, test_board_count)
{
	zassert_equal(0, pdc_power_mgmt_get_usb_pd_port_count());
}

ZTEST_USER(pdc_runtime_config_no_pdcs, test_board_get_drivers)
{
	zassert_is_null(pdc_power_mgmt_get_port_pdc_driver(0));
	zassert_is_null(pdc_power_mgmt_get_port_pdc_driver(1));
}

ZTEST_USER(pdc_runtime_config_no_pdcs, test_board_get_state)
{
	/* Ports could have been active, but are disabled */
	zassert_equal(PDC_DISABLED, pdc_power_mgmt_get_task_state(0));
	zassert_equal(PDC_DISABLED, pdc_power_mgmt_get_task_state(1));

	/* Non-existent ports beyond the board's max port count */
	zassert_equal(PDC_INVALID, pdc_power_mgmt_get_task_state(2));
	zassert_equal(PDC_INVALID, pdc_power_mgmt_get_task_state(3));
}
