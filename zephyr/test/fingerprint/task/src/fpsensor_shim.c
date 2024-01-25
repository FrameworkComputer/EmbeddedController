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
#include <fpsensor/fpsensor.h>
#include <fpsensor/fpsensor_detect.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

#define fp_sim DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))

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
	struct ec_response_fp_info info;

	/* We need to initialize driver first to initialize 'error' field */
	zassert_ok(fp_sensor_init());
	zassert_ok(fp_sensor_get_info(&info));

	zassert_equal(info.vendor_id, FOURCC('C', 'r', 'O', 'S'));
	zassert_equal(info.product_id, 0);
	/*
	 * Last 4 bits of hardware id is a year of sensor production,
	 * could differ between sensors.
	 */
	zassert_equal(info.model_id, 0);
	zassert_equal(info.version, 0);
	zassert_equal(info.frame_size, FINGERPRINT_SENSOR_REAL_IMAGE_SIZE(
					       DT_NODELABEL(fpsensor_sim)));
	zassert_equal(info.pixel_format, FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(
						 DT_NODELABEL(fpsensor_sim)));
	zassert_equal(info.width,
		      FINGERPRINT_SENSOR_RES_X(DT_NODELABEL(fpsensor_sim)));
	zassert_equal(info.height,
		      FINGERPRINT_SENSOR_RES_Y(DT_NODELABEL(fpsensor_sim)));
	zassert_equal(info.bpp,
		      FINGERPRINT_SENSOR_RES_BPP(DT_NODELABEL(fpsensor_sim)));
	zassert_equal(info.errors, FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN);
}

ZTEST_USER(fpsensor_shim, test_shim_get_info_failed)
{
	struct ec_response_fp_info info;
	struct fingerprint_sensor_state state;

	fingerprint_get_state(fp_sim, &state);
	state.get_info_result = -EINVAL;
	fingerprint_set_state(fp_sim, &state);

	zassert_equal(fp_sensor_get_info(&info), -EINVAL);
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
	state.finger_state = -EINVAL;
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
