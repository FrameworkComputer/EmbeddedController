/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <ec_commands.h>
#include <ec_tasks.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

#define fp_sim DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))

/*
 * Time we wait for fpsensor task to check if the finger was removed.
 * This must be greater or equal to FINGER_POLLING_DELAY.
 */
#define FPSENSOR_POLLING_DELAY_MS 100

ZTEST_USER(fpsensor_finger_presence, test_finger_down_mode)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_FINGER_DOWN,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Detect finger on the sensor. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_DOWN);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Confirm that fpsensor task is waiting for finger. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_DOWN);

	/* Confirm that detect mode was enabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_true(state.detect_mode);

	/* Disable finger detection */
	params.mode = 0;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_FINGER_DOWN);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Confirm that detect mode was disabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_false(state.detect_mode);
}

ZTEST_USER(fpsensor_finger_presence, test_finger_down_present)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_FINGER_DOWN,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Detect finger on the sensor. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_DOWN);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor and ping fpsensor task. */
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_FINGER_DOWN);

	/*
	 * Confirm that finger down flag is not set after the finger is
	 * detected.
	 */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_FINGER_DOWN);
}

ZTEST_USER(fpsensor_finger_presence, test_finger_down_partial)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_FINGER_DOWN,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Detect finger on the sensor. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_DOWN);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger partially on the sensor and ping fpsensor task. */
	state.finger_state = FINGERPRINT_FINGER_STATE_PARTIAL;
	fingerprint_set_state(fp_sim, &state);
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm MKBP event was not sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 0);

	/* Confirm that finger down flag is still set. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_DOWN);
}

ZTEST_USER(fpsensor_finger_presence, test_finger_down_no_finger)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_FINGER_DOWN,
	};
	struct ec_response_fp_mode response;

	/* Detect finger on the sensor. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_DOWN);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Ping fpsensor task, but don't put finger on the sensor. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm MKBP event was not sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 0);

	/* Confirm that finger down flag is still set. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_DOWN);
}

ZTEST_USER(fpsensor_finger_presence, test_finger_up_mode)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_FINGER_UP,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Detect finger up. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_UP);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Confirm that fpsensor task is waiting for finger up. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_UP);

	/* Confirm that detect mode was enabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_true(state.detect_mode);

	/* Disable finger up detection */
	params.mode = 0;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_FINGER_UP);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Confirm that detect mode was disabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_false(state.detect_mode);
}

ZTEST_USER(fpsensor_finger_presence, test_finger_up_no_finger_no_interrupt)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_FINGER_UP,
	};
	struct ec_response_fp_mode response;
	uint32_t fp_events;

	/* Detect finger up. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_UP);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Confirm that fpsensor task is waiting for finger up. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_UP);

	/* Check that no MKBP event was triggered yet. */
	zassert_equal(mkbp_send_event_fake.call_count, 0);

	/* Wait for fpsensor task to check the finger state. */
	k_msleep(FPSENSOR_POLLING_DELAY_MS);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_FINGER_UP);

	/* Confirm that finger up flag is not set. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_FINGER_UP);
}

ZTEST_USER(fpsensor_finger_presence,
	   test_finger_up_present_then_no_finger_interrupt)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_FINGER_UP,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Detect finger up. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_UP);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Remove finger from the sensor and ping fpsensor task. */
	state.finger_state = FINGERPRINT_FINGER_STATE_NONE;
	fingerprint_set_state(fp_sim, &state);
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_FINGER_UP);

	/* Confirm that finger up flag is not set after the finger is up. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_FINGER_UP);
}

ZTEST_USER(fpsensor_finger_presence,
	   test_finger_up_present_then_no_finger_no_interrupt)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_FINGER_UP,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Detect finger up. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_UP);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Remove finger from the sensor. */
	state.finger_state = FINGERPRINT_FINGER_STATE_NONE;
	fingerprint_set_state(fp_sim, &state);

	/* Wait for fpsensor task to check the finger state. */
	k_msleep(FPSENSOR_POLLING_DELAY_MS);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_FINGER_UP);

	/* Confirm that finger up flag is not set after the finger is up. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_FINGER_UP);
}

ZTEST_USER(fpsensor_finger_presence,
	   test_finger_up_present_then_partial_no_interrupt)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_FINGER_UP,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Detect finger up. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_UP);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger partially on the sensor. */
	state.finger_state = FINGERPRINT_FINGER_STATE_PARTIAL;
	fingerprint_set_state(fp_sim, &state);

	/* Wait for fpsensor task to check the finger state. */
	k_msleep(FPSENSOR_POLLING_DELAY_MS);

	/* Confirm MKBP event was not sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 0);

	/* Confirm that finger up flag is still set. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_UP);
}

ZTEST_USER(fpsensor_finger_presence,
	   test_finger_up_partial_then_no_finger_no_interrupt)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_FINGER_UP,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PARTIAL;
	fingerprint_set_state(fp_sim, &state);

	/* Detect finger up. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_FINGER_UP);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Remove finger from the sensor. */
	state.finger_state = FINGERPRINT_FINGER_STATE_NONE;
	fingerprint_set_state(fp_sim, &state);

	/* Wait for fpsensor task to check the finger state. */
	k_msleep(FPSENSOR_POLLING_DELAY_MS);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_FINGER_UP);

	/* Confirm that finger up flag is not set after the finger is up. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_FINGER_UP);
}

static void *fpsensor_setup(void)
{
	/* Start shimmed tasks. */
	start_ec_tasks();
	k_msleep(100);

	return NULL;
}

static void fpsensor_before(void *f)
{
	struct fingerprint_sensor_state state = {
		.bad_pixels = 0,
		.maintenance_ran = false,
		.detect_mode = false,
		.low_power_mode = false,
		.finger_state = FINGERPRINT_FINGER_STATE_NONE,
		.init_result = 0,
		.deinit_result = 0,
		.config_result = 0,
		.get_info_result = 0,
		.acquire_image_result = FINGERPRINT_SENSOR_SCAN_GOOD,
		.last_acquire_image_mode = -1,
	};
	struct ec_params_fp_mode params = {
		.mode = 0,
	};
	struct ec_response_fp_mode response;

	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_equal(response.mode, 0);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	fingerprint_set_state(fp_sim, &state);
	RESET_FAKE(mkbp_send_event);
}

ZTEST_SUITE(fpsensor_finger_presence, NULL, fpsensor_setup, fpsensor_before,
	    NULL, NULL);
