/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "test/drivers/utils.h"
#include "test_usbc_alt_mode.h"

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

	/* Verify that DisplayPort is the active alternate mode. */
	struct ec_params_usb_pd_get_mode_response response;
	int response_size;

	host_cmd_usb_pd_get_amode(TEST_PORT, 0, &response, &response_size);

	/* Response should be populated with a DisplayPort VDO */
	zassert_equal(response_size, sizeof(response));
	zassert_equal(response.svid, USB_SID_DISPLAYPORT);
	zassert_equal(response.vdo[0],
		      fixture->partner.modes_vdm[response.opos]);
}

ZTEST_F(usbc_alt_mode, verify_mode_entry_via_pd_host_cmd)
{
	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	/* Verify entering mode */
	struct ec_params_usb_pd_set_mode_request set_mode_params = {
		.cmd = PD_ENTER_MODE,
		.port = TEST_PORT,
		.opos = 1, /* Second VDO (after Discovery Responses) */
		.svid = USB_SID_DISPLAYPORT,
	};

	struct host_cmd_handler_args set_mode_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_USB_PD_SET_AMODE, 0, set_mode_params);

	zassert_ok(host_command_process(&set_mode_args));

	/* Verify that DisplayPort is the active alternate mode. */
	struct ec_params_usb_pd_get_mode_response get_mode_response;
	int response_size;

	host_cmd_usb_pd_get_amode(TEST_PORT, 0, &get_mode_response,
				  &response_size);

	/* Response should be populated with a DisplayPort VDO */
	zassert_equal(response_size, sizeof(get_mode_response));
	zassert_equal(get_mode_response.svid, USB_SID_DISPLAYPORT);
	zassert_equal(get_mode_response.vdo[0],
		      fixture->partner.modes_vdm[get_mode_response.opos]);
}

ZTEST_F(usbc_alt_mode, verify_mode_exit_via_pd_host_cmd)
{
	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_REQUIRE_AP_MODE_ENTRY)) {
		ztest_test_skip();
	}

	host_cmd_typec_control_enter_mode(TEST_PORT, TYPEC_MODE_DP);
	k_sleep(K_SECONDS(1));

	struct ec_params_usb_pd_get_mode_response get_mode_response;
	int response_size;

	host_cmd_usb_pd_get_amode(TEST_PORT, 0, &get_mode_response,
				  &response_size);

	/* We require an the active alternate mode (DisplayPort in this case),
	 * entering an alternate most (DisplayPort specifically) has already
	 * been verified in another test
	 */
	zassume_equal(response_size, sizeof(get_mode_response));
	zassume_equal(get_mode_response.svid, USB_SID_DISPLAYPORT);
	zassume_equal(get_mode_response.vdo[0],
		      fixture->partner.modes_vdm[get_mode_response.opos]);

	struct ec_params_usb_pd_set_mode_request set_mode_params = {
		.cmd = PD_EXIT_MODE,
		.port = TEST_PORT,
		.opos = get_mode_response.opos,
		.svid = get_mode_response.svid,
	};

	struct host_cmd_handler_args set_mode_args = BUILD_HOST_COMMAND_PARAMS(
		EC_CMD_USB_PD_SET_AMODE, 0, set_mode_params);

	zassert_ok(host_command_process(&set_mode_args));

	/* Verify mode was exited using get_amode command */
	host_cmd_usb_pd_get_amode(TEST_PORT, 0, &get_mode_response,
				  &response_size);
	zassert_not_equal(get_mode_response.vdo[0],
			  fixture->partner.modes_vdm[get_mode_response.opos]);
}
