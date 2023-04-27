/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "body_detection.h"
#include "console.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

static void body_detect_mode_before(void *state)
{
	ARG_UNUSED(state);
	body_detect_reset();
}

static void body_detect_mode_after(void *state)
{
	ARG_UNUSED(state);
	body_detect_reset();
}

/**
 * @brief TestPurpose: various body_detect_change_state operations.
 */
ZTEST_USER(bodydetectmode, test_body_detect_set_state)
{
	enum body_detect_states body_detect_state;

	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_ON_BODY,
		      "unexpected body detect initial mode: %d",
		      body_detect_state);

	body_detect_change_state(BODY_DETECTION_OFF_BODY, false);
	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_OFF_BODY,
		      "unexpected body detect mode: %d", body_detect_state);

	body_detect_change_state(BODY_DETECTION_ON_BODY, false);
	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_ON_BODY,
		      "unexpected body detect mode: %d", body_detect_state);
}

/**
 * @brief TestPurpose: ensure that console bodydetectmode forces the status,
 * inhibiting body_detect_change_state, and then unforce it with reset.
 */
ZTEST_USER(bodydetectmode, test_setbodydetectionmode_forced)
{
	int ret;
	enum body_detect_states body_detect_state;

	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_ON_BODY,
		      "unexpected body detect initial mode: %d",
		      body_detect_state);

	/**
	 * Set body detect mode to "off", since it defaults "on".
	 */
	ret = shell_execute_cmd(get_ec_shell(), "bodydetectmode off");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);
	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_OFF_BODY,
		      "unexpected body detect mode: %d", body_detect_state);

	/**
	 * Set body detect mode to "on", to validate it can be enabled also.
	 */
	ret = shell_execute_cmd(get_ec_shell(), "bodydetectmode on");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);
	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_ON_BODY,
		      "unexpected body detect mode: %d", body_detect_state);

	/**
	 * Reset body detect mode. This returns body detect to "on".
	 */
	ret = shell_execute_cmd(get_ec_shell(), "bodydetectmode reset");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);
	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_ON_BODY,
		      "unexpected body detect mode: %d", body_detect_state);
}

/**
 * @brief TestPurpose: check the "too many arguments" case.
 */
ZTEST_USER(bodydetectmode, test_setbodydetectionmode_too_many_args)
{
	int ret;

	ret = shell_execute_cmd(get_ec_shell(),
				"bodydetectmode too many arguments");
	zassert_equal(ret, EC_ERROR_PARAM_COUNT,
		      "unexpected command return status: %d", ret);
}

/**
 * @brief TestPurpose: check the "unknown argument" case.
 */
ZTEST_USER(bodydetectmode, test_setbodydetectionmode_unknown_arg)
{
	int ret;

	ret = shell_execute_cmd(get_ec_shell(), "bodydetectmode X");
	zassert_equal(ret, EC_ERROR_PARAM1,
		      "unexpected command return status: %d", ret);
}

ZTEST_SUITE(bodydetectmode, drivers_predicate_post_main, NULL,
	    body_detect_mode_before, body_detect_mode_after, NULL);
