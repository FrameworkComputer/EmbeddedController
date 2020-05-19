/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB common module.
 */
#include "test_util.h"
#include "usb_common.h"

int test_pd_get_cc_state(void)
{
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_RP_3_0),
		PD_CC_DFP_DEBUG_ACC, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_RP_1_5),
		PD_CC_DFP_DEBUG_ACC, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_RP_DEF),
		PD_CC_DFP_DEBUG_ACC, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_1_5, TYPEC_CC_VOLT_RP_3_0),
		PD_CC_DFP_DEBUG_ACC, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_1_5, TYPEC_CC_VOLT_RP_1_5),
		PD_CC_DFP_DEBUG_ACC, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_1_5, TYPEC_CC_VOLT_RP_DEF),
		PD_CC_DFP_DEBUG_ACC, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_DEF, TYPEC_CC_VOLT_RP_3_0),
		PD_CC_DFP_DEBUG_ACC, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_DEF, TYPEC_CC_VOLT_RP_1_5),
		PD_CC_DFP_DEBUG_ACC, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_DEF, TYPEC_CC_VOLT_RP_DEF),
		PD_CC_DFP_DEBUG_ACC, "%d");

	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_OPEN),
		PD_CC_DFP_ATTACHED, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_1_5, TYPEC_CC_VOLT_OPEN),
		PD_CC_DFP_ATTACHED, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RP_DEF, TYPEC_CC_VOLT_OPEN),
		PD_CC_DFP_ATTACHED, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_RP_3_0),
		PD_CC_DFP_ATTACHED, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_RP_1_5),
		PD_CC_DFP_ATTACHED, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_RP_DEF),
		PD_CC_DFP_ATTACHED, "%d");

	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_RD),
		PD_CC_UFP_DEBUG_ACC, "%d");

	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_RA),
		PD_CC_UFP_ATTACHED, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_OPEN),
		PD_CC_UFP_ATTACHED, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RA, TYPEC_CC_VOLT_RD),
		PD_CC_UFP_ATTACHED, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_RD),
		PD_CC_UFP_ATTACHED, "%d");

	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RA, TYPEC_CC_VOLT_RA),
		PD_CC_UFP_AUDIO_ACC, "%d");

	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_OPEN),
		PD_CC_NONE, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_RA),
		PD_CC_NONE, "%d");
	TEST_EQ(pd_get_cc_state(TYPEC_CC_VOLT_RA, TYPEC_CC_VOLT_OPEN),
		PD_CC_NONE, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	RUN_TEST(test_pd_get_cc_state);

	test_print_result();
}
