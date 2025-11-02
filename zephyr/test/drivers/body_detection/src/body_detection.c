/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "body_detection.h"
#include "console.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <math.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, get_data_rate, const struct motion_sensor_t *);
FAKE_VALUE_FUNC(int, get_rms_noise, const struct motion_sensor_t *);

extern struct motion_sensor_t *body_sensor;
extern float var_threshold;
extern float confidence_delta;

/*
 * In order to be independent from a motion sensor driver changes mock
 * two functions that are used during body detection parameters initialization.
 */
const static struct accelgyro_drv mock_drv = {
	.get_data_rate = get_data_rate,
	.get_rms_noise = get_rms_noise,
};
const static struct accelgyro_drv *old_drv;

static void body_detect_before(void *state)
{
	ARG_UNUSED(state);
	body_detect_reset();
}

static void body_detect_after(void *state)
{
	ARG_UNUSED(state);
	body_detect_reset();
}

/**
 * @brief TestPurpose: various body_detect_change_state operations.
 */
ZTEST_USER(bodydetectmode, test_body_detect_set_state)
{
	enum body_detect_states body_detect_state;

	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_ON_BODY,
		      "unexpected body detect initial mode: %d",
		      body_detect_state);

	body_detect_change_state(BODY_DETECTION_OFF_BODY, false);
	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_OFF_BODY,
		      "unexpected body detect mode: %d", body_detect_state);

	body_detect_change_state(BODY_DETECTION_ON_BODY, false);
	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_ON_BODY,
		      "unexpected body detect mode: %d", body_detect_state);
}

/**
 * @brief TestPurpose: ensure that console bodydetectmode forces the status,
 * inhibiting body_detect_change_state, and then unforce it with reset.
 */
ZTEST_USER(bodydetectmode, test_setbodydetectionmode_forced)
{
	int ret;
	enum body_detect_states body_detect_state;

	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_ON_BODY,
		      "unexpected body detect initial mode: %d",
		      body_detect_state);

	/**
	 * Set body detect mode to "off", since it defaults "on".
	 */
	ret = shell_execute_cmd(get_ec_shell(), "bodydetectmode off");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);
	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_OFF_BODY,
		      "unexpected body detect mode: %d", body_detect_state);

	/**
	 * Set body detect mode to "on", to validate it can be enabled also.
	 */
	ret = shell_execute_cmd(get_ec_shell(), "bodydetectmode on");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);
	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_ON_BODY,
		      "unexpected body detect mode: %d", body_detect_state);

	/**
	 * Set body detect mode to "off", since it defaults "on".
	 */
	ret = shell_execute_cmd(get_ec_shell(), "bodydetectmode off");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);
	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_OFF_BODY,
		      "unexpected body detect mode: %d", body_detect_state);

	/**
	 * Reset body detect mode. This returns body detect to "on".
	 * After a delay to analyze sensor data.
	 */
	ret = shell_execute_cmd(get_ec_shell(), "bodydetectmode reset");
	zassert_equal(ret, EC_SUCCESS, "unexpected command return status: %d",
		      ret);
	k_sleep(K_SECONDS(1));
	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_ON_BODY,
		      "unexpected body detect mode: %d", body_detect_state);
}

/**
 * @brief TestPurpose: check the "too many arguments" case.
 */
ZTEST_USER(bodydetectmode, test_setbodydetectionmode_too_many_args)
{
	int ret;

	ret = shell_execute_cmd(get_ec_shell(),
				"bodydetectmode too many arguments");
	zassert_equal(ret, EC_ERROR_PARAM_COUNT,
		      "unexpected command return status: %d", ret);
}

/**
 * @brief TestPurpose: check the "unknown argument" case.
 */
ZTEST_USER(bodydetectmode, test_setbodydetectionmode_unknown_arg)
{
	int ret;

	ret = shell_execute_cmd(get_ec_shell(), "bodydetectmode X");
	zassert_equal(ret, EC_ERROR_PARAM1,
		      "unexpected command return status: %d", ret);
}

ZTEST_SUITE(bodydetectmode, drivers_predicate_post_main, NULL,
	    body_detect_before, body_detect_after, NULL);

static void body_detect_init_before(void *state)
{
	ARG_UNUSED(state);

	RESET_FAKE(get_data_rate);
	RESET_FAKE(get_rms_noise);

	/* ODR = 50 Hz */
	get_data_rate_fake.return_val = 50 * 1000;
	/* RMS noise of LIS2DW12 with ODR set to 50Hz */
	get_rms_noise_fake.return_val = 636;

	/* Reset body detection to make sure the body_sensor is valid */
	body_detect_reset();

	old_drv = body_sensor->drv;
	body_sensor->drv = &mock_drv;
	body_sensor->bd_params = NULL;
	body_detect_reset();
}

static void body_detect_init_after(void *state)
{
	ARG_UNUSED(state);
	motion_sense_set_on_body_sensor_index(
		SENSOR_ID(DT_ALIAS(default_on_body_sensor)));

	body_sensor->drv = old_drv;
	body_sensor->bd_params = NULL;
	body_detect_reset();
}

#define DEFAULT_CONFIDENCE_DELTA CONFIG_BODY_DETECTION_CONFIDENCE_DELTA
#define DEFAULT_VAR_THRESHOLD CONFIG_BODY_DETECTION_VAR_THRESHOLD

/**
 * @brief TestPurpose: check variance properties with default input parameters
 */
ZTEST_USER(bodydetectinit, test_defaultparams)
{
	/*
	 * body_detect_reset was already called in body_detect_init_before.
	 * No need to invoke it here.
	 */
	zassert_equal(confidence_delta, DEFAULT_CONFIDENCE_DELTA);
	zassert_equal(var_threshold, DEFAULT_VAR_THRESHOLD);
	zassert_equal(1, get_data_rate_fake.call_count);
}

/**
 * @brief TestPurpose: check variance properties with custom parameters
 * If any parameter is set to zero it should be replaced with default
 * value read from Kconfig.
 */
ZTEST_USER(bodydetectinit, test_customparams)
{
	struct body_detect_params params;

	body_sensor->bd_params = &params;

	memset(&params, 0, sizeof(params));

	body_detect_reset();
	zassert_equal(confidence_delta, DEFAULT_CONFIDENCE_DELTA);
	zassert_equal(var_threshold, DEFAULT_VAR_THRESHOLD);
	zassert_equal(2, get_data_rate_fake.call_count);

	params.confidence_delta = 2900;
	params.var_threshold = 3000;

	body_detect_reset();
	zassert_equal(confidence_delta, 2900);
	zassert_equal(var_threshold, 3000);
	zassert_equal(3, get_data_rate_fake.call_count);
}

ZTEST_USER(bodydetectinit, test_change_sensor_index)
{
	zassert_not_equal(MOTION_SENSE_INVALID_SENSOR_ID,
			  motion_sense_get_on_body_sensor_index());
	motion_sense_set_on_body_sensor_index(MOTION_SENSE_INVALID_SENSOR_ID);
	zassert_equal(MOTION_SENSE_INVALID_SENSOR_ID,
		      motion_sense_get_on_body_sensor_index());
}

ZTEST_SUITE(bodydetectinit, drivers_predicate_post_main, NULL,
	    body_detect_init_before, body_detect_init_after, NULL);

void body_detect_step(float x, float y, float z, uint64_t curtime);
extern timestamp_t (*get_time_ptr)();
static timestamp_t (*get_time_ptr_old)();
static uint64_t time_fake_value;
static timestamp_t get_time_fake(void)
{
	timestamp_t time;

	time.val = time_fake_value;

	return time;
}

static void body_detect_sample_before(void *state)
{
	struct body_detect_params params;

	ARG_UNUSED(state);
	get_time_ptr_old = get_time_ptr;
	get_time_ptr = get_time_fake;

	old_drv = body_sensor->drv;
	body_sensor->drv = &mock_drv;

	RESET_FAKE(get_data_rate);
	RESET_FAKE(get_rms_noise);

	/* ODR = 50 Hz */
	get_data_rate_fake.return_val = 50 * 1000;
	/* RMS noise of LIS2DW12 with ODR set to 50Hz */
	get_rms_noise_fake.return_val = 636;

	body_sensor->bd_params = &params;
	memset(&params, 0, sizeof(params));

	params.confidence_delta = 3000;
	params.var_threshold = 4000;

	body_detect_reset();
	zassert_equal(confidence_delta, 3000);
	zassert_equal(var_threshold, 4000);
}

static void body_detect_sample_after(void *state)
{
	ARG_UNUSED(state);
	get_time_ptr = get_time_ptr_old;
	body_sensor->bd_params = NULL;
	body_sensor->drv = old_drv;

	body_detect_reset();
}

#define WHITE_NOISE(range) (rand() % range - range / 2)

/**
 * @brief TestPurpose: provide real-life data to feed the algorithm.
 */
ZTEST_USER(bodydetectsample, test_reallife_data)
{
	int i, cnt;
	int noise1, noise2;
	float fs = 50.0f;
	float vib_freq;
	int noise_range = 3;
	int G_1 = 16384;

	/* Simulation time is 2 minutes */
	cnt = 2 * 60 * fs;

	/*
	 * Run body_detect with small noise, should detect OFF_BODY.
	 */
	srand(0);
	for (i = 0; i < cnt; i++) {
		noise1 = WHITE_NOISE(noise_range);
		noise2 = WHITE_NOISE(noise_range);
		time_fake_value = (1000000 / fs * i);
		/* Average noise should sum up to 0 */
		body_sensor->xyz[X] = noise1;
		body_sensor->xyz[Y] = noise2;
		body_sensor->xyz[Z] = G_1 - noise1 - noise2;
		body_detect();
	}
	zassert_equal(BODY_DETECTION_OFF_BODY, body_detect_get_state(),
		      "expected state %d at %d[s]", BODY_DETECTION_OFF_BODY,
		      cnt);

	/*
	 * Run body_detect with small noise and 0.25Hz "breathing" signal
	 * in Z-axis, should detect ON_BODY.
	 */
	srand(0);
	vib_freq = 0.25f;
	for (i = 0; i < cnt; i++) {
		noise1 = WHITE_NOISE(noise_range);
		noise2 = WHITE_NOISE(noise_range);
		time_fake_value = (1000000 / fs * i);
		/* Average noise should sum up to 0 */
		body_sensor->xyz[X] = noise1;
		body_sensor->xyz[Y] = noise2;
		body_sensor->xyz[Z] = G_1 - noise1 - noise2;
		body_sensor->xyz[Z] +=
			sin(vib_freq * 2 * 3.14f / fs * i) * (G_1 / 50);
		body_detect();
	}
	zassert_equal(BODY_DETECTION_ON_BODY, body_detect_get_state(),
		      "expected state %d at %d[s]", BODY_DETECTION_ON_BODY,
		      cnt);

	/*
	 * Run body_detect with small noise and 5Hz "typing" resonance signal
	 * in Z-axis, should detect OFF_BODY.
	 */
	srand(0);
	vib_freq = 5;
	for (i = 0; i < cnt; i++) {
		noise1 = WHITE_NOISE(noise_range);
		noise2 = WHITE_NOISE(noise_range);
		time_fake_value = (1000000 / fs * i);
		/* Average noise should sum up to 0 */
		body_sensor->xyz[X] = noise1;
		body_sensor->xyz[Y] = noise2;
		body_sensor->xyz[Z] = G_1 - noise1 - noise2;
		body_sensor->xyz[Z] +=
			sin(vib_freq * 2 * 3.14f / fs * i) * (G_1 / 100);
		body_detect();
	}
	zassert_equal(BODY_DETECTION_OFF_BODY, body_detect_get_state(),
		      "expected state %d at %d[s]", BODY_DETECTION_OFF_BODY,
		      cnt);
}

ZTEST_SUITE(bodydetectsample, drivers_predicate_post_main, NULL,
	    body_detect_sample_before, body_detect_sample_after, NULL);

ZTEST_USER(bodydetectonoff, test_setenable)
{
	enum body_detect_states body_detect_state;
	int enabled;

	body_detect_state = body_detect_get_state();
	zassert_equal(body_detect_state, BODY_DETECTION_ON_BODY,
		      "unexpected body detect initial mode: %d",
		      body_detect_state);

	body_detect_set_enable(0);
	enabled = body_detect_get_enable();
	zassert_equal(enabled, 0, "unexpected state, enabled = %d", enabled);

	body_detect_set_enable(1);
	enabled = body_detect_get_enable();
	zassert_equal(enabled, 1, "unexpected state, enabled = %d", enabled);
}

ZTEST_SUITE(bodydetectonoff, drivers_predicate_post_main, NULL,
	    body_detect_before, body_detect_after, NULL);
