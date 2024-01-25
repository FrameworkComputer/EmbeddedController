/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock_fingerprint_algorithm.h"

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
#include <fingerprint/fingerprint_alg.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

#define fp_sim DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#define IMAGE_SIZE                          \
	FINGERPRINT_SENSOR_REAL_IMAGE_SIZE( \
		DT_CHOSEN(cros_fp_fingerprint_sensor))
static uint8_t image_buffer[IMAGE_SIZE];

static int enroll_percent;
static int enroll_step_return_val;
static int custom_enroll_step(const struct fingerprint_algorithm *const alg,
			      const uint8_t *const image, int *percent)
{
	zassert_mem_equal(image, image_buffer, IMAGE_SIZE);
	*percent = enroll_percent;

	return enroll_step_return_val;
}

ZTEST_USER(fpsensor_enroll, test_enroll_start_stop)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;

	/* Start enroll session. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Make sure the 'enroll_start' callback was called. */
	zassert_equal(mock_alg_enroll_start_fake.call_count, 1);

	/* Stop enroll session. */
	params.mode = 0;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode &
		      (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Confirm that enroll session is not running. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_ENROLL_SESSION);

	/* Make sure the 'enroll_finish' callback was called. */
	zassert_equal(mock_alg_enroll_finish_fake.call_count, 1);
}

ZTEST_USER(fpsensor_enroll, test_enroll_start_failure)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;

	/* Return failure on attempt to start enroll session. */
	mock_alg_enroll_start_fake.return_val = -EINVAL;

	/* Try to start enroll session. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Confirm that enroll session is not running. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_ENROLL_SESSION);

	/* Make sure that 'enroll_finish' callback was NOT called. */
	zassert_equal(mock_alg_enroll_finish_fake.call_count, 0);
}

ZTEST_USER(fpsensor_enroll, test_enroll_configure_detect)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Start enroll session. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Confirm that detect mode was enabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_true(state.detect_mode);

	/* Stop enroll session. */
	params.mode = 0;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode &
		      (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Confirm that detect mode was disabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_false(state.detect_mode);
}

ZTEST_USER(fpsensor_enroll, test_enroll_step)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Prepare image. */
	memset(image_buffer, 1, IMAGE_SIZE);

	/* Load image to simulator. */
	fingerprint_load_image(fp_sim, image_buffer, IMAGE_SIZE);

	/*
	 * Use custom enroll step function to confirm that provided image is
	 * correct and return enroll progress to fpsensor task.
	 */
	enroll_percent = 33;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_ENROLL
	 * - No errors
	 * - Reported enroll progress is correct
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_ENROLL);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events), 0);
	zassert_equal(EC_MKBP_FP_ENROLL_PROGRESS(fp_events), enroll_percent);
}

ZTEST_USER(fpsensor_enroll, test_enroll_step_failure)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Return critical error from enroll_step. */
	mock_alg_enroll_step_fake.return_val = -EINVAL;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_ENROLL
	 * - Internal error is reported
	 * - No progress is reported
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_ENROLL);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events),
		      EC_MKBP_FP_ERR_ENROLL_INTERNAL);
	zassert_equal(EC_MKBP_FP_ENROLL_PROGRESS(fp_events), 0);

	/* Confirm that enroll session is still running. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_ENROLL_SESSION);
}

ZTEST_USER(fpsensor_enroll, test_enroll_step_low_quality_warning)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Prepare image. */
	memset(image_buffer, 1, IMAGE_SIZE);

	/* Load image to simulator. */
	fingerprint_load_image(fp_sim, image_buffer, IMAGE_SIZE);

	/*
	 * Use custom enroll step function to warn about low quality of the
	 * image and provide enroll progress to fpsensor task.
	 */
	enroll_percent = 33;
	enroll_step_return_val = FP_ENROLLMENT_RESULT_LOW_QUALITY;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_ENROLL
	 * - Low Quality warning is reported
	 * - Correct progress is reported
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_ENROLL);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events),
		      EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY);
	zassert_equal(EC_MKBP_FP_ENROLL_PROGRESS(fp_events), enroll_percent);

	/* Confirm that enroll session is still running. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_ENROLL_SESSION);
}

ZTEST_USER(fpsensor_enroll, test_enroll_step_finish_failed)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Prepare image. */
	memset(image_buffer, 1, IMAGE_SIZE);

	/* Load image to simulator. */
	fingerprint_load_image(fp_sim, image_buffer, IMAGE_SIZE);

	/*
	 * Use custom enroll step function to confirm that provided image is
	 * correct and return enroll progress to fpsensor task.
	 */
	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Return error on call to enroll_finish. */
	mock_alg_enroll_finish_fake.return_val = -EINVAL;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_ENROLL
	 * - Internal error was returned
	 * - Reported enroll progress is correct
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_ENROLL);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events),
		      EC_MKBP_FP_ERR_ENROLL_INTERNAL);
	zassert_equal(EC_MKBP_FP_ENROLL_PROGRESS(fp_events), enroll_percent);

	/* Confirm that enroll session is not running. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_ENROLL_SESSION);
}

ZTEST_USER(fpsensor_enroll, test_enroll_step_finish_success)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_response_fp_info info;
	uint32_t fp_events;

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Prepare image. */
	memset(image_buffer, 1, IMAGE_SIZE);

	/* Load image to simulator. */
	fingerprint_load_image(fp_sim, image_buffer, IMAGE_SIZE);

	/*
	 * Use custom enroll step function to confirm that provided image is
	 * correct and return enroll progress to fpsensor task.
	 */
	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm that 'enroll_finish' was called. */
	zassert_equal(mock_alg_enroll_finish_fake.call_count, 1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_ENROLL
	 * - Internal error was returned
	 * - Reported enroll progress is correct
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_ENROLL);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events), 0);
	zassert_equal(EC_MKBP_FP_ENROLL_PROGRESS(fp_events), enroll_percent);

	/* Confirm that enroll session is not running. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_ENROLL_SESSION);

	/* Confirm that there is 1 valid template. */
	zassert_ok(ec_cmd_fp_info(NULL, &info));
	zassert_equal(info.template_valid, 1);
	/* Don't forget that template_dirty is a bitmask. */
	zassert_equal(info.template_dirty, 0x1);
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
	uint32_t fp_events;

	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_equal(response.mode, 0);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	enroll_percent = 0;
	enroll_step_return_val = 0;

	fingerprint_set_state(fp_sim, &state);
	RESET_FAKE(mkbp_send_event);

	RESET_FAKE(mock_alg_init);
	RESET_FAKE(mock_alg_exit);
	RESET_FAKE(mock_alg_enroll_start);
	RESET_FAKE(mock_alg_enroll_step);
	RESET_FAKE(mock_alg_enroll_finish);
	RESET_FAKE(mock_alg_match);

	/* Clear MKBP events from previous tests. */
	fp_get_next_event((uint8_t *)&fp_events);
}

ZTEST_SUITE(fpsensor_enroll, NULL, fpsensor_setup, fpsensor_before, NULL, NULL);
