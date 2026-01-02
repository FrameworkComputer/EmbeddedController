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
#include <fpsensor/fpsensor_frame_size.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

#define fp_sim DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#define IMAGE_SIZE                                                 \
	MAX_FROM_LIST(LISTIFY(NUM_IMAGE_CAPTURE_TYPES,             \
			      FINGERPRINT_SENSOR_FRAME_SIZE, (, ), \
			      DT_CHOSEN(cros_fp_fingerprint_sensor)))
static uint8_t image_buffer[IMAGE_SIZE];
static uint8_t frame_buffer[IMAGE_SIZE];

ZTEST_USER(fpsensor_capture, test_finger_capture_simple_image_detection_enabled)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_CAPTURE |
			(FP_CAPTURE_SIMPLE_IMAGE << FP_MODE_CAPTURE_TYPE_SHIFT),
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Switch mode to capture. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
	zassert_equal(FP_CAPTURE_TYPE(response.mode), FP_CAPTURE_SIMPLE_IMAGE);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Confirm that detect mode was enabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_true(state.detect_mode);

	/* Disable finger capture. */
	params.mode = 0;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_CAPTURE);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Confirm that detect mode was disabled. */
	fingerprint_get_state(fp_sim, &state);
	zassert_false(state.detect_mode);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_simple_image_mode_is_correct)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_CAPTURE |
			(FP_CAPTURE_SIMPLE_IMAGE << FP_MODE_CAPTURE_TYPE_SHIFT),
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Switch mode to capture. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
	zassert_equal(FP_CAPTURE_TYPE(response.mode), FP_CAPTURE_SIMPLE_IMAGE);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm that correct mode was passed to driver. */
	fingerprint_get_state(fp_sim, &state);
	zassert_equal(state.last_acquire_image_mode,
		      FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_finger_state_partial)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_CAPTURE |
			(FP_CAPTURE_SIMPLE_IMAGE << FP_MODE_CAPTURE_TYPE_SHIFT),
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Switch mode to capture. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
	zassert_equal(FP_CAPTURE_TYPE(response.mode), FP_CAPTURE_SIMPLE_IMAGE);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor (partially). */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PARTIAL;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm that no scan was performed. */
	fingerprint_get_state(fp_sim, &state);
	zassert_equal(state.last_acquire_image_mode, -1);

	/* Confirm that capture mode is still enabled. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_finger_state_none)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_CAPTURE |
			(FP_CAPTURE_SIMPLE_IMAGE << FP_MODE_CAPTURE_TYPE_SHIFT),
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Switch mode to capture. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
	zassert_equal(FP_CAPTURE_TYPE(response.mode), FP_CAPTURE_SIMPLE_IMAGE);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm that no scan was performed. */
	fingerprint_get_state(fp_sim, &state);
	zassert_equal(state.last_acquire_image_mode, -1);

	/* Confirm that capture mode is still enabled. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_simple_image_scan_too_fast)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_CAPTURE |
			(FP_CAPTURE_SIMPLE_IMAGE << FP_MODE_CAPTURE_TYPE_SHIFT),
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Switch mode to capture. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
	zassert_equal(FP_CAPTURE_TYPE(response.mode), FP_CAPTURE_SIMPLE_IMAGE);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	state.acquire_image_result = FINGERPRINT_SENSOR_SCAN_TOO_FAST;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm that capture mode is still enabled. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
}

ZTEST_USER(fpsensor_capture,
	   test_finger_capture_simple_image_scan_success_mode_cleared)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_CAPTURE |
			(FP_CAPTURE_SIMPLE_IMAGE << FP_MODE_CAPTURE_TYPE_SHIFT),
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Switch mode to capture. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
	zassert_equal(FP_CAPTURE_TYPE(response.mode), FP_CAPTURE_SIMPLE_IMAGE);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm that capture mode is not enabled. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_CAPTURE);
}

ZTEST_USER(fpsensor_capture,
	   test_finger_capture_simple_image_scan_success_mkbp_event)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_CAPTURE |
			(FP_CAPTURE_SIMPLE_IMAGE << FP_MODE_CAPTURE_TYPE_SHIFT),
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Switch mode to capture. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
	zassert_equal(FP_CAPTURE_TYPE(response.mode), FP_CAPTURE_SIMPLE_IMAGE);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task to process event. */
	k_msleep(1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/* Confirm that FP_IMAGE_READY MKBP event is sent. */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_IMAGE_READY);
}

ZTEST_USER(fpsensor_capture,
	   test_finger_capture_simple_image_scan_success_get_frame)
{
	FpFrameSizeCache frame_size_cache;
	frame_size_cache.populate_cache(IMAGE_SIZE);
	constexpr enum fp_capture_type kCaptureType = FP_CAPTURE_SIMPLE_IMAGE;
	const uint32_t image_size =
		frame_size_cache.get_frame_size(kCaptureType);
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_CAPTURE |
			(kCaptureType << FP_MODE_CAPTURE_TYPE_SHIFT),
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_frame frame_request = {
		.offset = FP_FRAME_INDEX_RAW_IMAGE << FP_FRAME_INDEX_SHIFT,
		.size = image_size,
	};

	/* Switch mode to capture. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
	zassert_equal(FP_CAPTURE_TYPE(response.mode), kCaptureType);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Prepare image. */
	memset(image_buffer, 1, IMAGE_SIZE);

	/* Load image to simulator. */
	fingerprint_load_image(fp_sim, image_buffer, image_size);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Get fingerprint raw image and compare buffers. */
	zassert_ok(ec_cmd_fp_frame(NULL, &frame_request, frame_buffer));
	zassert_mem_equal(frame_buffer, image_buffer, image_size);
}

ZTEST_USER(fpsensor_capture,
	   test_finger_capture_simple_image_scan_success_get_frame_v1)
{
	FpFrameSizeCache frame_size_cache;
	frame_size_cache.populate_cache(IMAGE_SIZE);
	constexpr enum fp_capture_type kCaptureType = FP_CAPTURE_SIMPLE_IMAGE;
	const uint32_t image_size =
		frame_size_cache.get_frame_size(kCaptureType);
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_CAPTURE |
			(kCaptureType << FP_MODE_CAPTURE_TYPE_SHIFT),
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_frame_v1 frame_request = {
		.cmd = FP_FRAME_GET_RAW_IMAGE,
		.offset = 0,
		.size = image_size,
	};

	/* Switch mode to capture. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_CAPTURE);
	zassert_equal(FP_CAPTURE_TYPE(response.mode), kCaptureType);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Prepare image. */
	memset(image_buffer, 1, IMAGE_SIZE);

	/* Load image to simulator. */
	fingerprint_load_image(fp_sim, image_buffer, image_size);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Get fingerprint raw image and compare buffers. */
	zassert_ok(ec_cmd_fp_frame_v1(NULL, &frame_request, frame_buffer));
	zassert_mem_equal(frame_buffer, image_buffer, image_size);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_get_frame_v1_size_too_big)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_CAPTURE |
			(FP_CAPTURE_SIMPLE_IMAGE << FP_MODE_CAPTURE_TYPE_SHIFT),
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint8_t buffer[IMAGE_SIZE + 1];
	struct ec_params_fp_frame_v1 frame_request = {
		.cmd = FP_FRAME_GET_RAW_IMAGE,
		.offset = 0,
		.size = IMAGE_SIZE + 1,
	};

	/* Switch mode to capture. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm that FP_FRAME host command will return an error. */
	zassert_equal(ec_cmd_fp_frame_v1(NULL, &frame_request, buffer),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(fpsensor_capture, test_finger_capture_get_frame_v1_bad_offset)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_CAPTURE |
			(FP_CAPTURE_SIMPLE_IMAGE << FP_MODE_CAPTURE_TYPE_SHIFT),
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_frame_v1 frame_request = {
		.cmd = FP_FRAME_GET_RAW_IMAGE,
		.offset = IMAGE_SIZE + 1,
		.size = 1,
	};

	/* Switch mode to capture. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Confirm that FP_FRAME host command will return an error. */
	zassert_equal(ec_cmd_fp_frame_v1(NULL, &frame_request, frame_buffer),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(fpsensor_capture,
	   test_finger_capture_get_frame_v1_invalid_capture_type)
{
	struct ec_params_fp_frame_v1 frame_request = {
		.cmd = FP_FRAME_GET_RAW_IMAGE,
		.offset = 0,
		.size = IMAGE_SIZE,
	};

	/* Confirm that FP_FRAME host command will return an error. */
	zassert_equal(ec_cmd_fp_frame_v1(NULL, &frame_request, frame_buffer),
		      EC_RES_INVALID_PARAM);
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

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	params.mode = FP_MODE_RESET_SENSOR;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	fingerprint_set_state(fp_sim, &state);
	RESET_FAKE(mkbp_send_event);
}

ZTEST_SUITE(fpsensor_capture, NULL, fpsensor_setup, fpsensor_before, NULL,
	    NULL);
