/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd_ap_vdm_control.h"

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define TEST_PORT USBC_PORT_C0

ZTEST_SUITE(ap_vdm_control_disabled, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);

ZTEST(ap_vdm_control_disabled, test_feature_absent)
{
	struct ec_response_get_features feat = host_cmd_get_features();

	zassert_false(feat.flags[1] &
		      EC_FEATURE_MASK_1(EC_FEATURE_TYPEC_AP_VDM_SEND));
}

ZTEST(ap_vdm_control_disabled, test_send_vdm_req_fails)
{
	struct ec_params_typec_control params = {
		.port = TEST_PORT,
		.command = TYPEC_CONTROL_COMMAND_SEND_VDM_REQ,
		.vdm_req_params = {
			.vdm_data = { 0 },
			.vdm_data_objects = 1,
			.partner_type = TYPEC_PARTNER_SOP,
		},
	};

	zassert_equal(ec_cmd_typec_control(NULL, &params),
		      EC_RES_INVALID_PARAM);
}

ZTEST(ap_vdm_control_disabled, test_vdm_response_fails)
{
	struct ec_response_typec_vdm_response vdm_resp;
	struct ec_params_typec_vdm_response params = { .port = TEST_PORT };

	zassert_equal(ec_cmd_typec_vdm_response(NULL, &params, &vdm_resp),
		      EC_RES_INVALID_COMMAND);
}
