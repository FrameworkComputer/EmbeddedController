/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test_new.h>

#include "ec_commands.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "mock/power.h"
#include "test/drivers/utils.h"
#include "test_usbc_alt_mode.h"

/* Tests that require CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY disabled
 */

ZTEST_F(usbc_alt_mode, verify_displayport_mode_power_cycle)
{
	/* Verify that the TCPM enters DP mode on attach, exits on AP power-off,
	 * and enters again on AP power on.
	 */

	zassert_true(fixture->partner.displayport_configured, NULL);

	mock_power_request(POWER_REQ_SOFT_OFF);

	zassert_false(fixture->partner.displayport_configured, NULL);

	mock_power_request(POWER_REQ_ON);
	zassert_true(fixture->partner.displayport_configured, NULL);

	struct ec_params_usb_pd_get_mode_response response;
	int response_size;

	host_cmd_usb_pd_get_amode(TEST_PORT, 0, &response, &response_size);

	zassert_equal(response_size, sizeof(response), NULL);
	zassert_equal(response.svid, USB_SID_DISPLAYPORT, NULL);
	zassert_equal(response.vdo[0],
		      fixture->partner.modes_vdm[response.opos], NULL);
}
