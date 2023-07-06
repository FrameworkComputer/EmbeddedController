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
