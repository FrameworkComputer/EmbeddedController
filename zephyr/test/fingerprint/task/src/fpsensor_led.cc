/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
extern "C" {
#include "pwm_mock.h"
}

#include <zephyr/drivers/pwm.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <fpsensor/fpsensor_led.h>
#include <mkbp_event.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

const struct device *pwm_fp_led =
	DEVICE_DT_GET(DT_PWMS_CTLR(DT_ALIAS(pwm_fp_led)));

static void test_fp_led_init(void *data)
{
	fp_led::update_mode(0);
}

static int fp_led_get()
{
	return pwm_mock_get_duty(pwm_fp_led, 0);
}

constexpr uint32_t BRIGHTNESS_ENROLL = 34;
constexpr uint32_t BRIGHTNESS_MATCH = 34;
constexpr uint32_t BRIGHTNESS_MATCH_OK = 100;
constexpr uint32_t BRIGHTNESS_MATCH_NOK = 6;
constexpr uint32_t BRIGHTNESS_OFF = 0;

ZTEST(fp_led, test_enroll)
{
	fp_led::update_mode(FP_MODE_ENROLL_SESSION);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_ENROLL, "Led %d%% after enroll",
		      BRIGHTNESS_ENROLL);
	k_sleep(K_SECONDS(30));
	zassert_equal(fp_led_get(), BRIGHTNESS_ENROLL,
		      "Led %d%% 30s after enroll", BRIGHTNESS_ENROLL);
}

ZTEST(fp_led, test_match)
{
	fp_led::update_mode(FP_MODE_MATCH);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_MATCH, "Led %d%% after match",
		      BRIGHTNESS_MATCH);
	k_sleep(K_SECONDS(10));
	zassert_equal(fp_led_get(), BRIGHTNESS_OFF, "Led off 10s after match");
}

ZTEST(fp_led, test_match_ok)
{
	fp_led::update_match(true);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_MATCH_OK,
		      "Led %d%% 0.5s after match OK", BRIGHTNESS_MATCH_OK);
	k_sleep(K_SECONDS(2));
	zassert_equal(fp_led_get(), BRIGHTNESS_OFF,
		      "Led off 2s after match OK");
}

ZTEST(fp_led, test_match_nok)
{
	fp_led::update_match(false);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_MATCH_NOK,
		      "Led %d%% after match NOK", BRIGHTNESS_MATCH_NOK);
	k_sleep(K_SECONDS(1));
	zassert_equal(fp_led_get(), BRIGHTNESS_OFF,
		      "Led off 1s after match NOK");
}

ZTEST(fp_led, test_off)
{
	fp_led::update_mode(FP_MODE_ENROLL_SESSION);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_ENROLL, "Led %d%% after enroll",
		      BRIGHTNESS_ENROLL);
	fp_led::update_mode(0);
	zassert_equal(fp_led_get(), BRIGHTNESS_OFF, "Led off immediately");
}

ZTEST(fp_led, test_off_do_not_disturb)
{
	fp_led::update_match(true);
	k_sleep(K_MSEC(500));
	zassert_equal(fp_led_get(), BRIGHTNESS_MATCH_OK,
		      "Led %d%% after match OK", BRIGHTNESS_MATCH_OK);
	fp_led::update_mode(0);
	zassert_equal(fp_led_get(), BRIGHTNESS_MATCH_OK, "Led still %d%%",
		      BRIGHTNESS_MATCH_OK);
	k_sleep(K_SECONDS(2));
	zassert_equal(fp_led_get(), BRIGHTNESS_OFF,
		      "Led off 2s after match OK");
}

ZTEST_SUITE(fp_led, NULL, NULL, test_fp_led_init, NULL, NULL);
