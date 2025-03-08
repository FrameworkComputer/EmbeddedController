/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/pdc_power_mgmt.h"

#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(pdc_power_mgmt_api_no_ccd, LOG_LEVEL_INF);

/*
 * This suite calls pdc_power_mgmt_get_sbu_mux_mode and
   pdc_power_mgmt_set_sbu_mux_mode with a devicetree overlay that does not have
   any CCD ports marked in the named-usbc-port nodes. With out this, calls to
   these functions should fail gracefully with -ENOTSUP.
 */
ZTEST_SUITE(pdc_power_mgmt_api_no_ccd, NULL, NULL, NULL, NULL, NULL);

ZTEST_USER(pdc_power_mgmt_api_no_ccd, test_get_sbu_mux_mode_no_ccd_ports)
{
	enum pdc_sbu_mux_mode mode;
	int port;

	zassert_equal(-ENOTSUP, pdc_power_mgmt_get_sbu_mux_mode(&mode, &port));
}

ZTEST_USER(pdc_power_mgmt_api_no_ccd, test_set_sbu_mux_mode_no_ccd_ports)
{
	zassert_equal(-ENOTSUP, pdc_power_mgmt_set_sbu_mux_mode(
					PDC_SBU_MUX_MODE_FORCE_DBG));
}
