/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test_new.h>

#include "ec_commands.h"
#include "suite.h"
#include "usb_common.h"
#include "usb_pd.h"

#define TEST_PORT 0

ZTEST_USER(usb_common, test_get_typec_current_limit_detached)
{
	/* If both CC lines are open, current limit should be 0 A. */
	typec_current_t current = usb_get_typec_current_limit(
		POLARITY_CC1, TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_OPEN);
	zassert_equal(current & TYPEC_CURRENT_ILIM_MASK, 0);
	zassert_equal(current & TYPEC_CURRENT_DTS_MASK, 0);
}

ZTEST_USER(usb_common, test_get_typec_current_limit_rp_default)
{
	/* USB Default current is 500 mA. */
	typec_current_t current = usb_get_typec_current_limit(
		POLARITY_CC1, TYPEC_CC_VOLT_RP_DEF, TYPEC_CC_VOLT_OPEN);
	zassert_equal(current & TYPEC_CURRENT_ILIM_MASK, 500);
	zassert_equal(current & TYPEC_CURRENT_DTS_MASK, 0);
}

ZTEST_USER(usb_common, test_get_typec_current_limit_rp_1500)
{
	typec_current_t current = usb_get_typec_current_limit(
		POLARITY_CC1, TYPEC_CC_VOLT_RP_1_5, TYPEC_CC_VOLT_OPEN);
	zassert_equal(current & TYPEC_CURRENT_ILIM_MASK, 1500);
	zassert_equal(current & TYPEC_CURRENT_DTS_MASK, 0);
}

ZTEST_USER(usb_common, test_get_typec_current_limit_rp_3000)
{
	typec_current_t current = usb_get_typec_current_limit(
		POLARITY_CC1, TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_OPEN);
	zassert_equal(current & TYPEC_CURRENT_ILIM_MASK, 3000);
	zassert_equal(current & TYPEC_CURRENT_DTS_MASK, 0);
}

ZTEST_USER(usb_common, test_get_typec_current_limit_rp_dts)
{
	/* For a DTS source, Rp 3A/Rp 1.5A indicates USB default current. The
	 * DTS flag should be set.
	 */
	typec_current_t current = usb_get_typec_current_limit(
		POLARITY_CC1, TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_RP_1_5);
	zassert_equal(current & TYPEC_CURRENT_ILIM_MASK, 500);
	zassert_equal(current & TYPEC_CURRENT_DTS_MASK, TYPEC_CURRENT_DTS_MASK);
}

ZTEST_USER(usb_common, test_get_snk_polarity)
{
	zassert_equal(get_snk_polarity(TYPEC_CC_VOLT_RP_3_0,
				       TYPEC_CC_VOLT_OPEN),
		      POLARITY_CC1);
	zassert_equal(get_snk_polarity(TYPEC_CC_VOLT_OPEN,
				       TYPEC_CC_VOLT_RP_3_0),
		      POLARITY_CC2);
}

ZTEST_USER(usb_common, test_get_snk_polarity_dts)
{
	zassert_equal(get_snk_polarity(TYPEC_CC_VOLT_RP_3_0,
				       TYPEC_CC_VOLT_RP_DEF),
		      POLARITY_CC1_DTS);
	zassert_equal(get_snk_polarity(TYPEC_CC_VOLT_RP_DEF,
				       TYPEC_CC_VOLT_RP_3_0),
		      POLARITY_CC2_DTS);
}

ZTEST_USER(usb_common, test_get_src_polarity)
{
	zassert_equal(get_src_polarity(TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_OPEN),
		      POLARITY_CC1);
	zassert_equal(get_src_polarity(TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_RD),
		      POLARITY_CC2);
}

ZTEST_USER(usb_common, test_pd_get_cc_state)
{
	zassert_equal(pd_get_cc_state(TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_RD),
		      PD_CC_UFP_DEBUG_ACC);
	zassert_equal(pd_get_cc_state(TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_OPEN),
		      PD_CC_UFP_ATTACHED);
	zassert_equal(pd_get_cc_state(TYPEC_CC_VOLT_RA, TYPEC_CC_VOLT_RA),
		      PD_CC_UFP_AUDIO_ACC);

	zassert_equal(pd_get_cc_state(TYPEC_CC_VOLT_RP_3_0,
				      TYPEC_CC_VOLT_RP_DEF),
		      PD_CC_DFP_DEBUG_ACC);
	zassert_equal(pd_get_cc_state(TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_OPEN),
		      PD_CC_DFP_ATTACHED);

	zassert_equal(pd_get_cc_state(TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_OPEN),
		      PD_CC_NONE);
}

ZTEST_USER(usb_common, test_pd_board_check_request_default)
{
	/* The default implementation accepts any RDO. Just use a basic one. */
	zassert_ok(pd_board_check_request(RDO_FIXED(0, 3000, 3000, 0), 1));
}
