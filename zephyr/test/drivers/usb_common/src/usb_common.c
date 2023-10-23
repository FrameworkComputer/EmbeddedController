/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "suite.h"
#include "timer.h"
#include "usb_common.h"
#include "usb_pd.h"

#include <stdint.h>

#include <zephyr/fff.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

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

ZTEST_USER(usb_common, test_pd_check_requested_voltage)
{
	uint32_t rdo;

	rdo = RDO_FIXED(1, 1000, 1500, 0);
	zassert_ok(pd_check_requested_voltage(rdo, 0));

	/* An index of 0 is invalid. */
	rdo = RDO_FIXED(0, 1000, 1500, 0);
	zassert_equal(pd_check_requested_voltage(rdo, 0), EC_ERROR_INVAL);
	/* So is an index larger than the number of source PDOs, which is 1 by
	 * default.
	 */
	rdo = RDO_FIXED(5, 1000, 1500, 0);
	zassert_equal(pd_check_requested_voltage(rdo, 0), EC_ERROR_INVAL);

	/* So is operating current too high. (This RDO doesn't make sense.) */
	rdo = RDO_FIXED(1, 1800, 1500, 0);
	zassert_equal(pd_check_requested_voltage(rdo, 0), EC_ERROR_INVAL);
	/* So is maximum current too high. */
	rdo = RDO_FIXED(1, 1000, 1800, 0);
	zassert_equal(pd_check_requested_voltage(rdo, 0), EC_ERROR_INVAL);
}

ZTEST_USER(usb_common, test_board_is_usb_pd_port_present)
{
	zassert_true(board_is_usb_pd_port_present(TEST_PORT));
	zassert_false(board_is_usb_pd_port_present(-1));
	zassert_false(board_is_usb_pd_port_present(100));
}

ZTEST_USER(usb_common, test_board_is_dts_port)
{
	zassert_true(board_is_dts_port(TEST_PORT));
}

ZTEST_USER(usb_common, test_drp_auto_toggle_next_state_detached)
{
	uint64_t drp_sink_time = 0;

	/* If the port is detached and toggle is disabled, the next state should
	 * be the configured default state.
	 */
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_TOGGLE_OFF,
			      TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_DEFAULT);

	/* If toggle is frozen, the next state should be the current state. */
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_FREEZE,
			      TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_UNATTACHED_SNK);
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SOURCE, PD_DRP_FREEZE,
			      TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_UNATTACHED_SRC);

	/* If role is forced, the next state should be the forced state. */
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_FORCE_SINK,
			      TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_UNATTACHED_SNK);
	zassert_equal(drp_auto_toggle_next_state(&drp_sink_time, PD_ROLE_SOURCE,
						 PD_DRP_FORCE_SOURCE,
						 TYPEC_CC_VOLT_OPEN,
						 TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_UNATTACHED_SRC);

	/* If toggle is enabled but auto-toggle is not supported, the next state
	 * should be based on the power role. If auto-toggle is supported, the
	 * next state should be auto-toggle.
	 */
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_TOGGLE_ON,
			      TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_OPEN, false),
		      DRP_TC_UNATTACHED_SNK);
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SOURCE, PD_DRP_TOGGLE_ON,
			      TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_OPEN, false),
		      DRP_TC_UNATTACHED_SRC);
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SOURCE, PD_DRP_TOGGLE_ON,
			      TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_DRP_AUTO_TOGGLE);
}

ZTEST_USER(usb_common, test_drp_auto_toggle_next_state_attached_to_source)
{
	uint64_t drp_sink_time = 0;

	/* If the CC lines show a source attached, then the next state should be
	 * a sink state. If auto-toggle is enabled, then the next state should
	 * assume that the TCPC is already in AttachWait.SNK.
	 */
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_TOGGLE_ON,
			      TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_OPEN, false),
		      DRP_TC_UNATTACHED_SNK);
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_TOGGLE_ON,
			      TYPEC_CC_VOLT_OPEN, TYPEC_CC_VOLT_RP_3_0, false),
		      DRP_TC_UNATTACHED_SNK);
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_TOGGLE_ON,
			      TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_ATTACHED_WAIT_SNK);

	/* If the DRP state is force-source, keep toggling. */
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_FORCE_SOURCE,
			      TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_OPEN, false),
		      DRP_TC_UNATTACHED_SNK);
	zassert_equal(drp_auto_toggle_next_state(&drp_sink_time, PD_ROLE_SOURCE,
						 PD_DRP_FORCE_SOURCE,
						 TYPEC_CC_VOLT_RP_3_0,
						 TYPEC_CC_VOLT_OPEN, false),
		      DRP_TC_UNATTACHED_SRC);
	zassert_equal(drp_auto_toggle_next_state(&drp_sink_time, PD_ROLE_SOURCE,
						 PD_DRP_FORCE_SOURCE,
						 TYPEC_CC_VOLT_RP_3_0,
						 TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_DRP_AUTO_TOGGLE);
}

ZTEST_USER(usb_common, test_drp_auto_toggle_next_state_attached_to_sink)
{
	uint64_t drp_sink_time = 0;
	timestamp_t fake_time;

	/* If the CC lines show a sink, then the next state should be a source
	 * state. If auto-toggle is enabled, then the next state should assume
	 * that the TCPC is already in AttachWait.SRC.
	 */
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SOURCE, PD_DRP_TOGGLE_ON,
			      TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_OPEN, false),
		      DRP_TC_UNATTACHED_SRC);
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SOURCE, PD_DRP_TOGGLE_ON,
			      TYPEC_CC_VOLT_RA, TYPEC_CC_VOLT_OPEN, false),
		      DRP_TC_UNATTACHED_SRC);
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_TOGGLE_ON,
			      TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_ATTACHED_WAIT_SRC);

	/* If the DRP state is off or force-sink, the TCPC might be in
	 * auto-toggle anyway. If the CC lines have been in this state for less
	 * than 100 ms, the TCPM should stay in Unattached.SNK and wait for the
	 * partner to toggle.
	 */
	drp_sink_time = 0;
	fake_time.val = drp_sink_time;
	get_time_mock = &fake_time;
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_TOGGLE_OFF,
			      TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_UNATTACHED_SNK);
	/* After 100 ms, the next state should be auto-toggle. */
	drp_sink_time = 0;
	fake_time.val = drp_sink_time + 105 * MSEC;
	get_time_mock = &fake_time;
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_TOGGLE_OFF,
			      TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_DRP_AUTO_TOGGLE);
	/* After 200 ms, the next state should be Unattached.SNK, and
	 * drp_sink_time should be updated.
	 */
	drp_sink_time = 0;
	fake_time.val = drp_sink_time + 205 * MSEC;
	get_time_mock = &fake_time;
	zassert_equal(drp_auto_toggle_next_state(
			      &drp_sink_time, PD_ROLE_SINK, PD_DRP_TOGGLE_OFF,
			      TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_OPEN, true),
		      DRP_TC_UNATTACHED_SNK);
	zassert_equal(drp_sink_time, fake_time.val);

	get_time_mock = NULL;
}
