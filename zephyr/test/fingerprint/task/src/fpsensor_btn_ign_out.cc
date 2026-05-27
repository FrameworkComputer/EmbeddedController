/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "fpsensor/fpsensor_btn_ign_out.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <cstdint>
#include <mkbp_event.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

static const struct gpio_dt_spec btn_ign_gpio =
	GPIO_DT_SPEC_GET(DT_ALIAS(gpio_btn_ign_out), gpios);

static void test_btn_ign_init(void *data)
{
	gpio_emul_input_set_dt(&btn_ign_gpio, 0);
}

static int btn_ign_get()
{
	return gpio_emul_output_get_dt(&btn_ign_gpio);
}

ZTEST_SUITE(fpsensor_btn_ign, NULL, NULL, test_btn_ign_init, NULL, NULL);

ZTEST(fpsensor_btn_ign, test_activate)
{
	fp_btn_ign_out::update(FP_MODE_MATCH);
	zassert_equal(btn_ign_get(), 1,
		      "GPIO pin should be high after activation");
}

ZTEST(fpsensor_btn_ign, test_deactivate)
{
	fp_btn_ign_out::update(FP_MODE_MATCH | FP_MODE_ENROLL_SESSION);
	k_sleep(K_MSEC(100));
	fp_btn_ign_out::update(0);

	k_sleep(K_MSEC(1000));
	zassert_equal(btn_ign_get(), 1, "GPIO should still be high");

	k_sleep(K_MSEC(1500));
	zassert_equal(btn_ign_get(), 0,
		      "GPIO pin should be low after deactivation");
}

ZTEST(fpsensor_btn_ign, test_activate_deactivate_consecutive_calls)
{
	fp_btn_ign_out::update(FP_MODE_MATCH);
	zassert_equal(btn_ign_get(), 1,
		      "GPIO should be high after first activate");

	fp_btn_ign_out::update(0);
	fp_btn_ign_out::update(FP_MODE_ENROLL_SESSION);

	k_sleep(K_MSEC(2500));
	zassert_equal(
		btn_ign_get(), 1,
		"GPIO should still be high due to cancelled deactivation");

	fp_btn_ign_out::update(0);
	k_sleep(K_MSEC(2500));
	zassert_equal(btn_ign_get(), 0,
		      "GPIO should be low after final deactivation");
}

ZTEST(fpsensor_btn_ign, test_deactivate_multiple_calls)
{
	fp_btn_ign_out::update(FP_MODE_CAPTURE);
	zassert_equal(btn_ign_get(), 1, "GPIO should be high after activate");

	fp_btn_ign_out::update(0);
	k_sleep(K_MSEC(1000));
	fp_btn_ign_out::update(0);

	k_sleep(K_MSEC(1500));
	zassert_equal(btn_ign_get(), 0,
		      "GPIO should be low 2s after first deactivation");
}
