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

/*
 * From USB Power Delivery Specification Revision 3.0, Version 2.0
 * Table 6-7 Power Data Object
 */
#define MAKE_FIXED(v, c) (0 << 30 | (v / 50) << 10 | (c / 10))
#define MAKE_BATT(v_max, v_min, p) \
	(1 << 30 | (v_max / 50) << 20 | (v_min / 50) << 10 | (p / 250))
#define MAKE_VAR(v_max, v_min, c) \
	(2 << 30 | (v_max / 50) << 20 | (v_min / 50) << 10 | (c / 10))
#define MAKE_AUG(v_max, v_min, c) \
	(3 << 30 | (v_max / 100) << 17 | (v_min / 100) << 8 | (c / 50))

/*
 * Tests various cases for pd_extract_pdo_power. It takes a very high voltage to
 * exceed PD_MAX_POWER_MW without also exceeding PD_MAX_CURRENT_MA, so those
 * tests are not particularly realistic.
 */
int test_pd_extract_pdo_power(void)
{
	uint32_t ma;
	uint32_t max_mv;
	uint32_t min_mv;

	pd_extract_pdo_power(MAKE_FIXED(/*v=*/5000, /*c=*/3000), &ma, &max_mv,
			     &min_mv);
	TEST_EQ(max_mv, 5000, "%d");
	TEST_EQ(min_mv, 5000, "%d");
	TEST_EQ(ma, 3000, "%d");
	pd_extract_pdo_power(MAKE_FIXED(/*v=*/20000, /*c=*/2600), &ma, &max_mv,
			     &min_mv);
	TEST_EQ(max_mv, 20000, "%d");
	TEST_EQ(min_mv, 20000, "%d");
	TEST_EQ(ma, 2600, "%d");
	pd_extract_pdo_power(MAKE_FIXED(/*v=*/20000, /*c=*/4000), &ma, &max_mv,
			     &min_mv);
	TEST_EQ(max_mv, 20000, "%d");
	TEST_EQ(min_mv, 20000, "%d");
	TEST_EQ(ma, 3000, "%d"); /* Capped at PD_MAX_CURRENT_MA */
	pd_extract_pdo_power(MAKE_FIXED(/*v=*/10000, /*c=*/4000), &ma, &max_mv,
			     &min_mv);
	TEST_EQ(max_mv, 10000, "%d");
	TEST_EQ(min_mv, 10000, "%d");
	TEST_EQ(ma, 3000, "%d"); /* Capped at PD_MAX_CURRENT_MA */
	pd_extract_pdo_power(MAKE_FIXED(/*v=*/21000, /*c=*/4000), &ma, &max_mv,
			     &min_mv);
	TEST_EQ(max_mv, 21000, "%d");
	TEST_EQ(min_mv, 21000, "%d");
	TEST_EQ(ma, 2857, "%d"); /* Capped at PD_MAX_POWER_MW */

	pd_extract_pdo_power(MAKE_BATT(/*v_max=*/5700, /*v_min=*/3300,
				       /*p=*/7000),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 5700, "%d");
	TEST_EQ(min_mv, 3300, "%d");
	TEST_EQ(ma, 2121, "%d"); /* 3300mV * 2121mA ~= 7000mW */
	pd_extract_pdo_power(MAKE_BATT(/*v_max=*/3300, /*v_min=*/2700,
				       /*p=*/12000),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 3300, "%d");
	TEST_EQ(min_mv, 2700, "%d");
	TEST_EQ(ma, 3000, "%d"); /* Capped at PD_MAX_CURRENT_MA */

	pd_extract_pdo_power(MAKE_BATT(/*v_max=*/25000, /*v_min=*/21000,
				       /*p=*/61000),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 25000, "%d");
	TEST_EQ(min_mv, 21000, "%d");
	TEST_EQ(ma, 2857, "%d"); /* Capped at PD_MAX_POWER_MW */

	pd_extract_pdo_power(MAKE_VAR(/*v_max=*/5000, /*v_min=*/3300,
				      /*c=*/3000),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 5000, "%d");
	TEST_EQ(min_mv, 3300, "%d");
	TEST_EQ(ma, 3000, "%d");
	pd_extract_pdo_power(MAKE_VAR(/*v_max=*/20000, /*v_min=*/5000,
				      /*c=*/2600),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 20000, "%d");
	TEST_EQ(min_mv, 5000, "%d");
	TEST_EQ(ma, 2600, "%d");
	pd_extract_pdo_power(MAKE_VAR(/*v_max=*/20000, /*v_min=*/5000,
				      /*c=*/4000),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 20000, "%d");
	TEST_EQ(min_mv, 5000, "%d");
	TEST_EQ(ma, 3000, "%d"); /* Capped at PD_MAX_CURRENT_MA */
	pd_extract_pdo_power(MAKE_VAR(/*v_max=*/10000, /*v_min=*/3300,
				      /*c=*/4000),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 10000, "%d");
	TEST_EQ(min_mv, 3300, "%d");
	TEST_EQ(ma, 3000, "%d"); /* Capped at PD_MAX_CURRENT_MA */
	pd_extract_pdo_power(MAKE_VAR(/*v_max=*/22000, /*v_min=*/21000,
				      /*c=*/4000),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 22000, "%d");
	TEST_EQ(min_mv, 21000, "%d");
	TEST_EQ(ma, 2857, "%d"); /* Capped at PD_MAX_POWER_MW */

	pd_extract_pdo_power(MAKE_AUG(/*v_max=*/5000, /*v_min=*/3300,
				      /*c=*/3000),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 5000, "%d");
	TEST_EQ(min_mv, 3300, "%d");
	TEST_EQ(ma, 3000, "%d");
	pd_extract_pdo_power(MAKE_AUG(/*v_max=*/20000, /*v_min=*/3300,
				      /*c=*/2600),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 20000, "%d");
	TEST_EQ(min_mv, 3300, "%d");
	TEST_EQ(ma, 2600, "%d");
	pd_extract_pdo_power(MAKE_AUG(/*v_max=*/10000, /*v_min=*/3300,
				      /*c=*/4000),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 10000, "%d");
	TEST_EQ(min_mv, 3300, "%d");
	TEST_EQ(ma, 3000, "%d"); /* Capped at PD_MAX_CURRENT_MA */
	pd_extract_pdo_power(MAKE_AUG(/*v_max=*/22000, /*v_min=*/21000,
				      /*c=*/4000),
			     &ma, &max_mv, &min_mv);
	TEST_EQ(max_mv, 22000, "%d");
	TEST_EQ(min_mv, 21000, "%d");
	TEST_EQ(ma, 2857, "%d"); /* Capped at PD_MAX_POWER_MW */

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	RUN_TEST(test_pd_get_cc_state);
	RUN_TEST(test_pd_extract_pdo_power);

	test_print_result();
}
