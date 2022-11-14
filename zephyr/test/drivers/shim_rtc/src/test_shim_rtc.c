/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "lpc.h"
#include "system.h"
#include "test/drivers/test_state.h"

#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

ZTEST_USER(rtc_shim, test_hc_rtc_set_get_value)
{
	struct ec_params_rtc set_value;
	struct ec_params_rtc get_value;
	struct host_cmd_handler_args set_args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_RTC_SET_VALUE, 0, set_value);
	struct host_cmd_handler_args get_args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_RTC_GET_VALUE, 0, get_value);

	/* Initially set/get arbitrary value */
	set_value.time = 1337;
	zassert_ok(host_command_process(&set_args));
	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_value.time, set_value.time);

	/* One more time to be sure the test is creating the value change */
	set_value.time = 1776;
	zassert_ok(host_command_process(&set_args));
	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_value.time, set_value.time);
}

ZTEST_USER(rtc_shim, test_hc_rtc_set_get_alarm)
{
	struct ec_params_rtc set_value;
	struct ec_params_rtc get_value;
	struct host_cmd_handler_args set_args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_RTC_SET_ALARM, 0, set_value);
	struct host_cmd_handler_args get_args =
		BUILD_HOST_COMMAND_RESPONSE(EC_CMD_RTC_GET_ALARM, 0, get_value);

	/* Initially set/get zero value */
	set_value.time = 0;
	zassert_ok(host_command_process(&set_args));
	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_value.time, set_value.time);

	/* One more time to be sure the test is creating the value change */
	set_value.time = 1776;
	zassert_ok(host_command_process(&set_args));
	zassert_ok(host_command_process(&get_args));
	zassert_equal(get_value.time, set_value.time);
}

ZTEST(rtc_shim, test_hc_rtc_set_alarm_can_fire_cb)
{
	struct ec_params_rtc set_value;
	struct host_cmd_handler_args set_args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_RTC_SET_ALARM, 0, set_value);

#ifdef CONFIG_HOSTCMD_X86
	/* Enable the RTC event to fire */
	host_event_t lpc_event_mask;
	host_event_t mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_RTC);

	lpc_event_mask = lpc_get_host_event_mask(LPC_HOST_EVENT_SMI);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, lpc_event_mask | mask);
#endif

	/* Arbitrary value of 2 */
	system_set_rtc(2);

	/* Clear events so we can check that RTC event happened */
	host_clear_events(CONFIG_HOST_EVENT_REPORT_MASK);
	zassert_false(host_is_event_set(EC_HOST_EVENT_RTC));

	/* Initially set arbitrary value of alarm in 2 seconds*/
	set_value.time = 2;
	zassert_ok(host_command_process(&set_args));
	/* Set fake driver time forward to hit the alarm in 2 seconds */
	system_set_rtc(4);

	/* Wait for irq to finish */
	k_sleep(K_SECONDS(1));
	zassert_true(host_is_event_set(EC_HOST_EVENT_RTC));
}

ZTEST_SUITE(rtc_shim, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
