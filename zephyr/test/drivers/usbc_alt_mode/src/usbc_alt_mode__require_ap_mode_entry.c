/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test/drivers/utils.h"
#include "test_usbc_alt_mode.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/* Tests that require CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY enabled */

ZTEST_F(usbc_alt_mode, verify_displayport_mode_reentry)
{
	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	/* DPM configures the partner on DP mode entry */
	/* Verify port partner thinks its configured for DisplayPort */
	zassert_true(fixture->partner.displayport_configured);

	host_cmd_typec_control_exit_modes(TEST_PORT);
	k_sleep(K_SECONDS(1));
	zassert_false(fixture->partner.displayport_configured);

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));
	zassert_true(fixture->partner.displayport_configured);

	/*
	 * Verify that DisplayPort is the active alternate mode by checking our
	 * MUX settings
	 */
	struct ec_response_typec_status status;

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_DP_ENABLED),
		      USB_PD_MUX_DP_ENABLED, "Failed to see DP mux set");
}

ZTEST_F(usbc_alt_mode, verify_mode_exit_via_pd_host_cmd)
{
	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	host_cmd_typec_control_exit_modes(TEST_PORT);
	k_sleep(K_SECONDS(1));
	zassert_false(fixture->partner.displayport_configured);

	/*
	 * Verify that DisplayPort is no longer active by checking our
	 * MUX settings
	 */
	struct ec_response_typec_status status;

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal((status.mux_state & USB_PD_MUX_DP_ENABLED), 0);
}
