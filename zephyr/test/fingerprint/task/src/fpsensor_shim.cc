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
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <fpsensor/fpsensor.h>
#include <fpsensor/fpsensor_detect.h>
#include <mkbp_event.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

#define fp_sim DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))

#define FP_SIMULATOR_IMAGE_FRAME_PARAM_INITIALIZER(idx, node_id)            \
	{                                                                   \
		.frame_size = FINGERPRINT_SENSOR_FRAME_SIZE(idx, node_id),  \
		.image_data_offset_bytes =                                  \
			FINGERPRINT_SENSOR_IMAGE_OFFSET(idx, node_id),      \
		.pixel_format =                                             \
			FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(idx, node_id), \
		.width = FINGERPRINT_SENSOR_RES_X(idx, node_id),            \
		.height = FINGERPRINT_SENSOR_RES_Y(idx, node_id),           \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(idx, node_id),            \
		.fp_capture_type =                                          \
			FINGERPRINT_SENSOR_CAPTURE_TYPE(idx, node_id),      \
		.reserved = 0,                                              \
	}

static const struct fingerprint_image_frame_params
	expected_image_frame_params_array[] = { LISTIFY(
		NUM_IMAGE_CAPTURE_TYPES,
		FP_SIMULATOR_IMAGE_FRAME_PARAM_INITIALIZER, (, ),
		DT_NODELABEL(fpsensor_sim)) };

static const size_t test_info_buffer_size =
	sizeof(struct ec_response_fp_info_v3) +
	sizeof(struct fp_image_frame_params_v2) * FP_MAX_CAPTURE_TYPES;
static uint8_t buffer[test_info_buffer_size];
static struct ec_response_fp_info_v3 *test_info_buffer =
	(struct ec_response_fp_info_v3 *)buffer;

ZTEST_USER(fpsensor_shim, test_shim_sensor_type_elan)
{
	const struct gpio_dt_spec *sensor_sel_pin =
		GPIO_DT_FROM_NODELABEL(fp_sensor_sel);

	gpio_emul_input_set(sensor_sel_pin->port, sensor_sel_pin->pin, 0);
	zassert_equal(fpsensor_detect_get_type(), FP_SENSOR_TYPE_ELAN);
}

ZTEST_USER(fpsensor_shim, test_shim_sensor_type_fpc)
{
	const struct gpio_dt_spec *sensor_sel_pin =
		GPIO_DT_FROM_NODELABEL(fp_sensor_sel);

	gpio_emul_input_set(sensor_sel_pin->port, sensor_sel_pin->pin, 1);
	zassert_equal(fpsensor_detect_get_type(), FP_SENSOR_TYPE_FPC);
}

ZTEST_USER(fpsensor_shim, test_shim_init_success)
{
	zassert_ok(fp_sensor_init());
}

ZTEST_USER(fpsensor_shim, test_shim_init_sensor_init_failed)
{
	struct fingerprint_sensor_state state;

	fingerprint_get_state(fp_sim, &state);
	state.init_result = -EINVAL;
	fingerprint_set_state(fp_sim, &state);

	zassert_equal(fp_sensor_init(), -EINVAL);
}

ZTEST_USER(fpsensor_shim, test_shim_init_algorithm_init_failed)
{
	mock_alg_init_fake.return_val = -EINVAL;

	zassert_equal(fp_sensor_init(), -EINVAL);
	zassert_equal(mock_alg_init_fake.call_count, 1);
}

ZTEST_USER(fpsensor_shim, test_shim_init_sensor_config_failed)
{
	struct fingerprint_sensor_state state;

	fingerprint_get_state(fp_sim, &state);
	state.config_result = -EINVAL;
	fingerprint_set_state(fp_sim, &state);

	zassert_equal(fp_sensor_init(), -EINVAL);
}

ZTEST_USER(fpsensor_shim, test_shim_deinit_success)
{
	zassert_ok(fp_sensor_init());
	zassert_ok(fp_sensor_deinit());
}

ZTEST_USER(fpsensor_shim, test_shim_deinit_algorithm_exit_failed)
{
	zassert_ok(fp_sensor_init());

	mock_alg_exit_fake.return_val = -EINVAL;
	zassert_equal(fp_sensor_deinit(), -EINVAL);
	zassert_equal(mock_alg_exit_fake.call_count, 1);
}

ZTEST_USER(fpsensor_shim, test_shim_deinit_sensor_deinit_failed)
{
	struct fingerprint_sensor_state state;

	zassert_ok(fp_sensor_init());

	fingerprint_get_state(fp_sim, &state);
	state.deinit_result = -EINVAL;
	fingerprint_set_state(fp_sim, &state);

	zassert_equal(fp_sensor_deinit(), -EINVAL);
}

ZTEST_USER(fpsensor_shim, test_shim_get_info_success)
{
	/* We need to initialize driver first to initialize 'error' field */
	zassert_ok(fp_sensor_init());
	zassert_ok(fp_sensor_get_info(test_info_buffer, test_info_buffer_size));

	zassert_equal(test_info_buffer->sensor_info.vendor_id,
		      FOURCC('C', 'r', 'O', 'S'));
	zassert_equal(test_info_buffer->sensor_info.product_id, 0);
	/*
	 * Last 4 bits of hardware id is a year of sensor production,
	 * could differ between sensors.
	 */
	zassert_equal(test_info_buffer->sensor_info.model_id, 0);
	zassert_equal(test_info_buffer->sensor_info.version, 0);
	zassert_equal(test_info_buffer->sensor_info.errors,
		      FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN);

	for (int i = 0; i < NUM_IMAGE_CAPTURE_TYPES; ++i) {
		zassert_equal(
			memcmp(&test_info_buffer->image_frame_params[i],
			       &expected_image_frame_params_array[i],
			       sizeof(struct fingerprint_image_frame_params)),
			0, "Struct comparison failed at index %d", i);
	}
}

ZTEST_USER(fpsensor_shim, test_shim_get_info_failed)
{
	struct fingerprint_sensor_state state;

	fingerprint_get_state(fp_sim, &state);
	state.get_info_result = -EINVAL;
	fingerprint_set_state(fp_sim, &state);

	zassert_equal(fp_sensor_get_info(test_info_buffer,
					 test_info_buffer_size),
		      -EINVAL);
}

ZTEST_USER(fpsensor_shim, test_shim_finger_status_present)
{
	struct fingerprint_sensor_state state;

	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	zassert_equal(fp_finger_status(), FINGER_PRESENT);
}

ZTEST_USER(fpsensor_shim, test_shim_finger_status_error)
{
	struct fingerprint_sensor_state state;

	fingerprint_get_state(fp_sim, &state);
	state.finger_state = static_cast<fingerprint_finger_state>(-EINVAL);
	fingerprint_set_state(fp_sim, &state);

	zassert_equal(fp_finger_status(), FINGER_NONE);
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

	fingerprint_set_state(fp_sim, &state);

	RESET_FAKE(mock_alg_init);
	RESET_FAKE(mock_alg_exit);
	RESET_FAKE(mock_alg_enroll_start);
	RESET_FAKE(mock_alg_enroll_step);
	RESET_FAKE(mock_alg_enroll_finish);
	RESET_FAKE(mock_alg_match);
}

ZTEST_SUITE(fpsensor_shim, NULL, NULL, fpsensor_before, NULL, NULL);
