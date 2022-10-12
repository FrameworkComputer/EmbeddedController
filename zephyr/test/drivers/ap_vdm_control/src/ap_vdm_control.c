/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

#include "ec_commands.h"
#include "usb_mux.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

static void ap_vdm_control_before(void *data)
{
	/* Set chipset on so the "AP" is on to give us commands */
	test_set_chipset_to_s0();
}

ZTEST_SUITE(ap_vdm_control, drivers_predicate_post_main, NULL,
	    ap_vdm_control_before, NULL, NULL);

ZTEST(ap_vdm_control, test_feature_present)
{
	struct ec_response_get_features feat = host_cmd_get_features();

	zassert_true(feat.flags[1] &
			     EC_FEATURE_MASK_1(EC_FEATURE_TYPEC_AP_VDM_SEND),
		     "Failed to see feature present");
}
