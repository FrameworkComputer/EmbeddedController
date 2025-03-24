/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/pdc.h"
#include "usbc/pdc_power_mgmt.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/ztest.h>

/** Enable both ports */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	switch (port) {
	case 0:
		*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_emul1));
		return 0;
	case 1:
		*dev = DEVICE_DT_GET(DT_NODELABEL(pdc_emul2));
		return 0;
	}

	*dev = NULL;
	return -ERANGE;
}

ZTEST_SUITE(pdc_runtime_config_two_pdcs, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(pdc_runtime_config_two_pdcs, test_board_count)
{
	zassert_equal(2, pdc_power_mgmt_get_usb_pd_port_count());
}

ZTEST_USER(pdc_runtime_config_two_pdcs, test_board_get_drivers)
{
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_emul1)),
		      pdc_power_mgmt_get_port_pdc_driver(0));
	zassert_equal(DEVICE_DT_GET(DT_NODELABEL(pdc_emul2)),
		      pdc_power_mgmt_get_port_pdc_driver(1));
}

ZTEST_USER(pdc_runtime_config_two_pdcs, test_board_get_state)
{
	/* Ports could have been active, but are disabled */
	zassert_equal(PDC_UNATTACHED, pdc_power_mgmt_get_task_state(0));
	zassert_equal(PDC_UNATTACHED, pdc_power_mgmt_get_task_state(1));

	/* Non-existent ports beyond the board's max port count */
	zassert_equal(PDC_INVALID, pdc_power_mgmt_get_task_state(2));
	zassert_equal(PDC_INVALID, pdc_power_mgmt_get_task_state(3));
}

ZTEST_USER(pdc_runtime_config_two_pdcs, test_board_get_info)
{
	struct pdc_info_t info;
	int rv;

	rv = pdc_power_mgmt_get_info(0, &info, true);
	zassert_ok(rv, "Got error result: %d", rv);

	rv = pdc_power_mgmt_get_info(1, &info, true);
	zassert_ok(rv, "Got error result: %d", rv);
}
