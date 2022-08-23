/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/shell/shell.h>
#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "tablet_mode.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

static void tabletmode_before(void *state)
{
	ARG_UNUSED(state);
	tablet_reset();
}

static void tabletmode_after(void *state)
{
	ARG_UNUSED(state);
	tablet_reset();
}

/**
 * @brief TestPurpose: various tablet_set_mode operations, make sure lid and
 * base works independently.
 */
ZTEST_USER(tabletmode, test_tablet_set_mode)
{
	int ret;

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexepcted tablet initial mode: %d", ret);

	tablet_set_mode(1, TABLET_TRIGGER_LID);

	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexepcted tablet mode: %d", ret);

	tablet_set_mode(1, TABLET_TRIGGER_BASE);

	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexepcted tablet mode: %d", ret);

	tablet_set_mode(0, TABLET_TRIGGER_LID);

	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexepcted tablet mode: %d", ret);

	tablet_set_mode(0, TABLET_TRIGGER_BASE);

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexepcted tablet mode: %d", ret);
}

/**
 * @brief TestPurpose: test the tablet_disable functionality.
 */
ZTEST_USER(tabletmode, test_tablet_disable)
{
	int ret;

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexepcted tablet initial mode: %d", ret);

	tablet_disable();
	tablet_set_mode(1, TABLET_TRIGGER_LID);

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexepcted tablet mode: %d", ret);
}

/**
 * @brief TestPurpose: check that tabletmode on and off changes the mode.
 */
ZTEST_USER(tabletmode, test_settabletmode_on_off)
{
	int ret;

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexepcted tablet initial mode: %d", ret);

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode");
	zassert_equal(ret, EC_SUCCESS, "unexepcted command return status: %d",
		      ret);

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexepcted tablet mode: %d", ret);

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode on");
	zassert_equal(ret, EC_SUCCESS, "unexepcted command return status: %d",
		      ret);

	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexepcted tablet mode: %d", ret);

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode off");
	zassert_equal(ret, EC_SUCCESS, "unexepcted command return status: %d",
		      ret);

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexepcted tablet mode: %d", ret);
}

/**
 * @brief TestPurpose: ensure that console tabletmode forces the status,
 * inhibiting tablet_set_mode, and then unforce it with reset.
 */
ZTEST_USER(tabletmode, test_settabletmode_forced)
{
	int ret;

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexepcted tablet initial mode: %d", ret);

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode on");
	zassert_equal(ret, EC_SUCCESS, "unexepcted command return status: %d",
		      ret);

	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexepcted tablet mode: %d", ret);

	tablet_set_mode(0, TABLET_TRIGGER_LID);

	ret = tablet_get_mode();
	zassert_equal(ret, 1, "unexepcted tablet mode: %d", ret);

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode reset");
	zassert_equal(ret, EC_SUCCESS, "unexepcted command return status: %d",
		      ret);

	tablet_set_mode(0, TABLET_TRIGGER_LID);

	ret = tablet_get_mode();
	zassert_equal(ret, 0, "unexepcted tablet mode: %d", ret);
}

/**
 * @brief TestPurpose: check the "too many arguments" case.
 */
ZTEST_USER(tabletmode, test_settabletmode_too_many_args)
{
	int ret;

	ret = shell_execute_cmd(get_ec_shell(),
				"tabletmode too many arguments");
	zassert_equal(ret, EC_ERROR_PARAM_COUNT,
		      "unexepcted command return status: %d", ret);
}

/**
 * @brief TestPurpose: check the "unknown argument" case.
 */
ZTEST_USER(tabletmode, test_settabletmode_unknown_arg)
{
	int ret;

	ret = shell_execute_cmd(get_ec_shell(), "tabletmode X");
	zassert_equal(ret, EC_ERROR_PARAM1,
		      "unexepcted command return status: %d", ret);
}

ZTEST_SUITE(tabletmode, drivers_predicate_post_main, NULL, tabletmode_before,
	    tabletmode_after, NULL);
