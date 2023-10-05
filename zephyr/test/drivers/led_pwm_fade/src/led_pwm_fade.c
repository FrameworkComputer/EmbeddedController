/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "ec_commands.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "gpio.h"
#include "include/power.h"
#include "led.h"
#include "led_common.h"
#include "pwm_mock.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

ZTEST_SUITE(led_pwm_fade, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST(led_pwm_fade, test_led_fade)
{
	const struct device *pwm_blue_left =
		DEVICE_DT_GET(DT_NODELABEL(pwm_blue_left));
	const struct device *pwm_white_left =
		DEVICE_DT_GET(DT_NODELABEL(pwm_white_left));
	const struct device *pwm_amber_right =
		DEVICE_DT_GET(DT_NODELABEL(pwm_amber_right));
	const struct device *pwm_white_right =
		DEVICE_DT_GET(DT_NODELABEL(pwm_white_right));

	/* make sure we're starting at the start of a pattern */
	test_set_chipset_to_g3();
	k_sleep(K_SECONDS(1));

	test_set_chipset_to_s0();
	k_sleep(K_SECONDS(2));
	/* left LED should be at about 50% blue */
	zassert_true(pwm_mock_get_duty(pwm_blue_left, 0) > 40, NULL);
	zassert_true(pwm_mock_get_duty(pwm_blue_left, 0) < 60, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_right, 0), 0, NULL);

	k_sleep(K_SECONDS(2));
	/* left LED should be at about 100% blue */
	zassert_true(pwm_mock_get_duty(pwm_blue_left, 0) > 90, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_right, 0), 0, NULL);

	k_sleep(K_SECONDS(1));
	/* left LED should be at about 75% blue */
	zassert_true(pwm_mock_get_duty(pwm_blue_left, 0) > 65, NULL);
	zassert_true(pwm_mock_get_duty(pwm_blue_left, 0) < 85, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_right, 0), 0, NULL);

	k_sleep(K_SECONDS(8));
	/* After a full cycle, the color remains the same. */
	zassert_true(pwm_mock_get_duty(pwm_blue_left, 0) > 65, NULL);
	zassert_true(pwm_mock_get_duty(pwm_blue_left, 0) < 85, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_right, 0), 0, NULL);

	test_set_chipset_to_power_level(POWER_S3);
	k_sleep(K_SECONDS(4));
	/* right LED should be at about 1% white */
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) < 5, NULL);

	k_sleep(K_SECONDS(1));
	/* right LED should be at about 10% white */
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) > 5, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) < 20, NULL);

	k_sleep(K_SECONDS(1));
	/* right LED should be at about 100% white */
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) > 20, NULL);

	k_sleep(K_SECONDS(1));
	/* right LED should flatten out at exactly 100% white */
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_right, 0), 100, NULL);

	k_sleep(K_SECONDS(2));
	/* right LED should be at about 10% white */
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_amber_right, 0), 0, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) > 5, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) < 20, NULL);

	test_set_chipset_to_power_level(POWER_S5);
	/*
	 * The emulator takes about 1 second to set power level to S5 but the
	 * LED responds immediately so the first k_sleep is for 1 second
	 * shorter than the actual half-period of 2s to compensate.
	 *
	 * TODO: find out why setting power level to S5 has a 1s delay and
	 * whether it is intended behavior.
	 */
	k_sleep(K_SECONDS(1));
	/* right LED should be at about 50% white */
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_true(pwm_mock_get_duty(pwm_amber_right, 0) < 10, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) > 40, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) < 60, NULL);

	k_sleep(K_SECONDS(2));
	/* right LED should be at about 100% white */
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_true(pwm_mock_get_duty(pwm_amber_right, 0) < 10, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) > 90, NULL);

	k_sleep(K_SECONDS(2));
	/* right LED should be at about 50% amber, 60% white */
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_true(pwm_mock_get_duty(pwm_amber_right, 0) > 40, NULL);
	zassert_true(pwm_mock_get_duty(pwm_amber_right, 0) < 60, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) > 50, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) < 70, NULL);

	k_sleep(K_SECONDS(2));
	/* right LED should be at about 100% amber, 20% white */
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_true(pwm_mock_get_duty(pwm_amber_right, 0) > 90, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) > 10, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) < 30, NULL);

	k_sleep(K_SECONDS(2));
	/* right LED should be at about 50% amber, 10% white */
	zassert_equal(pwm_mock_get_duty(pwm_blue_left, 0), 0, NULL);
	zassert_equal(pwm_mock_get_duty(pwm_white_left, 0), 0, NULL);
	zassert_true(pwm_mock_get_duty(pwm_amber_right, 0) > 40, NULL);
	zassert_true(pwm_mock_get_duty(pwm_amber_right, 0) < 60, NULL);
	zassert_true(pwm_mock_get_duty(pwm_white_right, 0) < 20, NULL);
}
