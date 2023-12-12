/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "mock/power.h"
#include "test/drivers/utils.h"
#include "test_usbc_alt_mode.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

/* Tests that require CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY disabled
 */

ZTEST_F(usbc_alt_mode, test_verify_displayport_mode_power_cycle)
{
	struct ec_response_typec_status status;

	/* Verify that the TCPM enters DP mode on attach, exits on AP power-off,
	 * and enters again on AP power on.
	 */

	zassert_true(fixture->partner.displayport_configured, NULL);
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_DP_ENABLED),
		      USB_PD_MUX_DP_ENABLED);

	mock_power_request(POWER_REQ_SOFT_OFF);

	zassert_false(fixture->partner.displayport_configured, NULL);
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_DP_ENABLED), 0);

	mock_power_request(POWER_REQ_ON);

	zassert_true(fixture->partner.displayport_configured, NULL);
	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_DP_ENABLED),
		      USB_PD_MUX_DP_ENABLED);
}
