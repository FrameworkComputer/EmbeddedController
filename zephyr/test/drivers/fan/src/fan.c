/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "fan.h"
#include "gpio.h"
#include "include/power.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/ztest.h>

#define GPIO_PG_PATH NAMED_GPIOS_GPIO_NODE(test)
#define GPIO_PG_PORT DT_GPIO_PIN(GPIO_PG_PATH, gpios)

void pwm_fan_init(void);

struct fan_common_fixture {
	const struct device *pwm_mock;
	const struct device *tach_mock;
	const struct device *pgood_pin;
};

static void *fan_common_setup(void)
{
	static struct fan_common_fixture fixture;

	fixture.pwm_mock = DEVICE_DT_GET(DT_NODELABEL(pwm_fan));
	fixture.tach_mock = DEVICE_DT_GET(DT_NODELABEL(tach_fan));
	fixture.pgood_pin = DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_PG_PATH, gpios));

	return &fixture;
}

static void fan_common_before(void *data)
{
	struct fan_common_fixture *fixture = (struct fan_common_fixture *)data;

	/* Always start in S0, the state fans usually are on in */
	test_set_chipset_to_s0();

	/* Restore how many fans we have */
	fan_set_count(CONFIG_FANS);

	/* Ensure PGOOD pin is set */
	zassert_ok(gpio_emul_input_set(fixture->pgood_pin, GPIO_PG_PORT, 1));
}

ZTEST_SUITE(fan_common, drivers_predicate_post_main, fan_common_setup,
	    fan_common_before, NULL, NULL);

ZTEST(fan_common, test_faninfo)
{
	CHECK_CONSOLE_CMD("faninfo", "Duty:", EC_SUCCESS);
}

ZTEST(fan_common, test_fanauto)
{
	CHECK_CONSOLE_CMD("fanauto", NULL, EC_SUCCESS);
}

ZTEST(fan_common, test_fanset_no_fans)
{
	/* Pretend we have no fans */
	fan_set_count(0);

	CHECK_CONSOLE_CMD("fanset", "zero", EC_ERROR_INVAL);
}

ZTEST(fan_common, test_fanset_no_arg)
{
	CHECK_CONSOLE_CMD("fanset", NULL, EC_ERROR_PARAM_COUNT);
}

ZTEST(fan_common, test_fanset_too_many_args)
{
	CHECK_CONSOLE_CMD("fanset 1 2 3 4 5", NULL, EC_ERROR_PARAM_COUNT);
}

ZTEST(fan_common, test_fanset_bad_fan)
{
	CHECK_CONSOLE_CMD("fanset 80 0", NULL, EC_ERROR_PARAM1);
}

ZTEST(fan_common, test_fanset_valid_2_arg)
{
	CHECK_CONSOLE_CMD("fanset 0 80", "Setting fan", EC_SUCCESS);
}

ZTEST(fan_common, test_fanset_valid_1_arg)
{
	CHECK_CONSOLE_CMD("fanset 80%", "Setting fan", EC_SUCCESS);
}

ZTEST(fan_common, test_fanduty_no_fans)
{
	/* Pretend we have no fans */
	fan_set_count(0);

	CHECK_CONSOLE_CMD("fanduty", "zero", EC_ERROR_INVAL);
}

ZTEST(fan_common, test_fanduty_no_arg)
{
	CHECK_CONSOLE_CMD("fanduty", NULL, EC_ERROR_PARAM_COUNT);
}

ZTEST(fan_common, test_fanduty_too_many_args)
{
	CHECK_CONSOLE_CMD("fanduty 1 2 3 4 5", NULL, EC_ERROR_PARAM_COUNT);
}

ZTEST(fan_common, test_fanduty_bad_fan)
{
	CHECK_CONSOLE_CMD("fanduty 80 0", NULL, EC_ERROR_PARAM1);
}

ZTEST(fan_common, test_fanduty_valid_2_arg)
{
	CHECK_CONSOLE_CMD("fanduty 0 80", "Setting fan", EC_SUCCESS);
}

ZTEST(fan_common, test_fanduty_valid_1_arg)
{
	CHECK_CONSOLE_CMD("fanduty 80", "Setting fan", EC_SUCCESS);
}

static const int temp_fan_off = C_TO_K(20);
static const int temp_fan_max = C_TO_K(40);
static const struct fan_step_1_1 test_table[] = {
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(20),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(34),
	  .rpm = 1000 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(39),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(40),
	  .rpm = 2000 },
};

static bool change;

static void test_on_change(void)
{
	change = true;
}

ZTEST(fan_common, test_fan_hysteresis)
{
	/* Start the fan off */
	change = false;
	fan_set_rpm_target(0, 0);

	/* Start the temperature below off threshold */
	zassert_equal(temp_ratio_to_rpm_hysteresis(test_table, 2, 0,
						   TEMP_TO_RATIO(10),
						   &test_on_change),
		      0);
	zassert_false(change);

	/* Increase, but stay below level 0 increase threshold */
	zassert_equal(temp_ratio_to_rpm_hysteresis(test_table, 2, 0,
						   TEMP_TO_RATIO(30),
						   &test_on_change),
		      0);
	zassert_false(change);

	/* Increase to level 0 */
	zassert_equal(temp_ratio_to_rpm_hysteresis(test_table, 2, 0,
						   TEMP_TO_RATIO(35),
						   &test_on_change),
		      1000);
	zassert_true(change);

	/* Increase to level 1 */
	change = false;
	zassert_equal(temp_ratio_to_rpm_hysteresis(test_table, 2, 0,
						   TEMP_TO_RATIO(45),
						   &test_on_change),
		      2000);
	zassert_true(change);

	/* Decrease back to level 0 */
	change = false;
	zassert_equal(temp_ratio_to_rpm_hysteresis(test_table, 2, 0,
						   TEMP_TO_RATIO(38),
						   &test_on_change),
		      1000);
	zassert_true(change);
}

ZTEST(fan_common, test_fan_hc_get_target_rpm)
{
	struct ec_response_pwm_get_fan_rpm r;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_PWM_GET_FAN_TARGET_RPM, 0, r);

	zassert_ok(host_command_process(&args));
	zassert_equal(r.rpm, fan_get_rpm_target(0));
}

ZTEST(fan_common, test_fan_hc_get_target_rpm_no_fans)
{
	struct ec_response_pwm_get_fan_rpm r;
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_PWM_GET_FAN_TARGET_RPM, 0, r);

	/* Pretend we have no fans */
	fan_set_count(0);

	zassert_equal(host_command_process(&args), EC_RES_ERROR);
}

ZTEST(fan_common, test_fan_hc_set_target_rpm_v0)
{
	struct ec_params_pwm_set_fan_target_rpm_v0 p = {
		.rpm = 4000,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_PWM_SET_FAN_TARGET_RPM, 0, p);

	zassert_ok(host_command_process(&args));
	zassert_equal(p.rpm, fan_get_rpm_target(0));
}

ZTEST(fan_common, test_fan_hc_set_target_rpm_v1)
{
	struct ec_params_pwm_set_fan_target_rpm_v1 p = {
		.rpm = 4000,
		.fan_idx = 0,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_PWM_SET_FAN_TARGET_RPM, 1, p);

	zassert_ok(host_command_process(&args));
	zassert_equal(p.rpm, fan_get_rpm_target(0));
}

ZTEST(fan_common, test_fan_hc_set_target_rpm_v1_bad_fan)
{
	struct ec_params_pwm_set_fan_target_rpm_v1 p = {
		.rpm = 4000,
		.fan_idx = 80,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_PWM_SET_FAN_TARGET_RPM, 1, p);

	zassert_equal(host_command_process(&args), EC_RES_ERROR);
}

ZTEST(fan_common, test_fan_hc_set_duty_v0)
{
	struct ec_params_pwm_set_fan_duty_v0 p = {
		.percent = 50,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_PWM_SET_FAN_DUTY, 0, p);

	zassert_ok(host_command_process(&args));
}

ZTEST(fan_common, test_fan_hc_set_duty_v1)
{
	struct ec_params_pwm_set_fan_duty_v1 p = {
		.percent = 50,
		.fan_idx = 0,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_PWM_SET_FAN_DUTY, 1, p);

	zassert_ok(host_command_process(&args));
}

ZTEST(fan_common, test_fan_hc_set_duty_v1_bad_fan)
{
	struct ec_params_pwm_set_fan_duty_v1 p = {
		.percent = 50,
		.fan_idx = 20,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_PWM_SET_FAN_DUTY, 1, p);

	zassert_equal(host_command_process(&args), EC_RES_ERROR);
}

void set_thermal_control_enabled(int fan, int enable);

ZTEST(fan_common, test_fan_hc_set_auto_fan_v0)
{
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(EC_CMD_THERMAL_AUTO_FAN_CTRL, 0);

	set_thermal_control_enabled(0, 0);
	zassert_ok(host_command_process(&args));
	zassert_true(is_thermal_control_enabled(0));
}

ZTEST(fan_common, test_fan_hc_set_auto_fan_v1)
{
	struct ec_params_auto_fan_ctrl_v1 p = {
		.fan_idx = 0,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_THERMAL_AUTO_FAN_CTRL, 1, p);

	set_thermal_control_enabled(0, 0);
	zassert_ok(host_command_process(&args));
	zassert_true(is_thermal_control_enabled(0));
}

ZTEST(fan_common, test_fan_hc_set_auto_fan_v1_bad_fan)
{
	struct ec_params_auto_fan_ctrl_v1 p = {
		.fan_idx = 20,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_THERMAL_AUTO_FAN_CTRL, 1, p);

	zassert_equal(host_command_process(&args), EC_RES_ERROR);
}

ZTEST(fan_common, test_memmap_not_present)
{
	uint16_t *mapped = (uint16_t *)host_get_memmap(EC_MEMMAP_FAN);

	/* Initial reported speeds are zero. */
	for (int i = 0; i < EC_FAN_SPEED_ENTRIES; i++) {
		mapped[i] = 0;
	}

	/* Reported speeds are set to NOT_PRESENT as appropriate. */
	fan_set_count(0);
	pwm_fan_init();
	for (int i = 0; i < EC_FAN_SPEED_ENTRIES; i++) {
		zassert_equal(
			mapped[i], EC_FAN_SPEED_NOT_PRESENT,
			"Fan %d reports speed %d but should not be present", i,
			mapped[i]);
	}
}
