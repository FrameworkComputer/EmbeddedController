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
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

#define fp_sim DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))

ZTEST_USER(fpsensor_init, test_tpm_seed_init)
{
	struct ec_response_fp_encryption_status status;
	struct ec_params_fp_seed params = {
		.struct_version = 4,
		.reserved = 0,
		.seed = "very_secret_32_byte_of_tpm_seed",
	};

	/* Get FP encryption flags. */
	zassert_ok(ec_cmd_fp_encryption_status(NULL, &status));

	/* Confirm TPM seed is not set */
	zassert_true(status.valid_flags & FP_ENC_STATUS_SEED_SET);
	zassert_false(status.status & FP_ENC_STATUS_SEED_SET);

	/* Set TPM seed. */
	zassert_ok(ec_cmd_fp_seed(NULL, &params));

	/* Get FP encryption flags. */
	zassert_ok(ec_cmd_fp_encryption_status(NULL, &status));

	/* Confirm that FP_ENC_STATUS_SEED_SET is set. */
	zassert_true(status.valid_flags & FP_ENC_STATUS_SEED_SET);
	zassert_true(status.status & FP_ENC_STATUS_SEED_SET);

	/* Try to set TPM seed once again (should fail). */
	zassert_equal(EC_RES_ACCESS_DENIED, ec_cmd_fp_seed(NULL, &params));
}

ZTEST_USER(fpsensor_init, test_tpm_seed_invalid)
{
	struct ec_params_fp_seed params = {
		/* 0 is not a valid structure version. */
		.struct_version = 0,
		.reserved = 0,
		.seed = "very_secret_32_byte_of_tpm_seed",
	};

	/* Try to set TPM seed (should fail). */
	zassert_equal(EC_RES_INVALID_PARAM, ec_cmd_fp_seed(NULL, &params));
}

ZTEST_USER(fpsensor_init, test_set_fp_context)
{
	struct ec_params_fp_context_v1 params = {
		.action = FP_CONTEXT_ASYNC,
		.userid = { 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8 },
	};
	struct ec_response_fp_encryption_status status;

	/* Set context (asynchronously). */
	zassert_ok(ec_cmd_fp_context_v1(NULL, &params));

	/* Now any attempt to set context should return EC_RES_BUSY. */
	zassert_equal(EC_RES_BUSY, ec_cmd_fp_context_v1(NULL, &params));

	/* Now any attempt to get command result should return EC_RES_BUSY. */
	params.action = FP_CONTEXT_GET_RESULT;
	zassert_equal(EC_RES_BUSY, ec_cmd_fp_context_v1(NULL, &params));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Get command result. */
	zassert_ok(ec_cmd_fp_context_v1(NULL, &params));

	/* Get FP encryption flags. */
	zassert_ok(ec_cmd_fp_encryption_status(NULL, &status));

	/* Confirm that FP_CONTEXT_USER_ID_SET is set. */
	zassert_true(status.status & FP_CONTEXT_USER_ID_SET);
}

ZTEST_USER(fpsensor_init, test_maintenance_mode)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_SENSOR_MAINTENANCE,
	};
	struct ec_response_fp_mode response;
	struct ec_response_fp_info info;
	struct fingerprint_sensor_state state;

	const int dead_pixels = 3;

	/* Confirm that number of dead pixels is unknown. */
	zassert_ok(ec_cmd_fp_info(NULL, &info));
	zassert_equal(FP_ERROR_DEAD_PIXELS(info.errors),
		      FP_ERROR_DEAD_PIXELS_UNKNOWN);

	fingerprint_get_state(fp_sim, &state);
	state.bad_pixels = dead_pixels;
	fingerprint_set_state(fp_sim, &state);

	/* Change fingerprint mode to maintenance. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_SENSOR_MAINTENANCE);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Check that maintenance was run. */
	fingerprint_get_state(fp_sim, &state);
	zassert_true(state.maintenance_ran);

	/* Confirm that number of dead pixels is correct. */
	zassert_ok(ec_cmd_fp_info(NULL, &info));
	zassert_equal(FP_ERROR_DEAD_PIXELS(info.errors), dead_pixels);

	/*
	 * Confirm that maintenance flag is not set after the maintenance
	 * operation is finished.
	 */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_SENSOR_MAINTENANCE);
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

ZTEST_SUITE(fpsensor_init, NULL, fpsensor_setup, fpsensor_before, NULL, NULL);
