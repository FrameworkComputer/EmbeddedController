/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "body_detection.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "lid_switch.h"
#include "math_util.h"
#include "mkbp_input_devices.h"
#include "motion_sense_fifo.h"
#include "timer.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_ACCEL, outstr)
#define CPRINTS(format, args...) cprints(CC_ACCEL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)

const static struct body_detect_params default_body_detect_params = {
	.var_noise_factor = CONFIG_BODY_DETECTION_VAR_NOISE_FACTOR,
	.var_threshold = CONFIG_BODY_DETECTION_VAR_THRESHOLD,
	.confidence_delta = CONFIG_BODY_DETECTION_CONFIDENCE_DELTA,
};

static struct motion_sensor_t *body_sensor =
	&motion_sensors[CONFIG_BODY_DETECTION_SENSOR];

static int window_size = CONFIG_BODY_DETECTION_MAX_WINDOW_SIZE;
static uint64_t var_threshold_scaled, confidence_delta_scaled;
static int stationary_timeframe;

static int history_idx;
static enum body_detect_states motion_state = BODY_DETECTION_OFF_BODY;

static bool history_initialized;
static bool body_detect_enable;
STATIC_IF(CONFIG_ACCEL_SPOOF_MODE) bool spoof_enable;

static struct body_detect_motion_data {
	int history[CONFIG_BODY_DETECTION_MAX_WINDOW_SIZE]; /* acceleration */
	int sum; /* sum(history) */
	uint64_t n2_variance; /* n^2 * var(history) */
} data[2]; /* motion data for X-axis and Y-axis */

static void print_body_detect_mode(void)
{
	CPRINTS("body detect mode %sabled",
		body_detect_get_state() ? "en" : "dis");
}

/*
 * This function will update new variance and new sum according to incoming
 * value, previous value, previous sum and previous variance.
 * In order to prevent inaccuracy, we use integer to calculate instead of float
 *
 * n: window size
 * x: data in the old window
 * x': data in the new window
 * x_0: oldest value in the window, will be replaced by x_n
 * x_n: new coming value
 *
 * n^2 * var(x') = n^2 * var(x) + (x_n - x_0) *
 *                 (n * (x_n + x_0) - sum(x') - sum(x))
 */
static void update_motion_data(struct body_detect_motion_data *x, int x_n)
{
	const int n = window_size;
	const int x_0 = x->history[history_idx];
	const int sum_diff = x_n - x_0;
	const int new_sum = x->sum + sum_diff;

	x->n2_variance +=
		sum_diff * ((int64_t)n * (x_n + x_0) - new_sum - x->sum);
	x->sum = new_sum;
	x->history[history_idx] = x_n;
}

/* Update motion data of X, Y with new sensor data. */
static void update_motion_variance(void)
{
	update_motion_data(&data[X], body_sensor->xyz[X]);
	update_motion_data(&data[Y], body_sensor->xyz[Y]);
	history_idx = (history_idx + 1 >= window_size) ? 0 : history_idx + 1;
}

/* return Var(X) + Var(Y) */
static uint64_t get_motion_variance(void)
{
	return (data[X].n2_variance + data[Y].n2_variance) / window_size /
	       window_size;
}

static int calculate_motion_confidence(uint64_t var)
{
	if (var < var_threshold_scaled - confidence_delta_scaled)
		return 0;
	if (var > var_threshold_scaled + confidence_delta_scaled)
		return 100;
	return 100 * (var - var_threshold_scaled + confidence_delta_scaled) /
	       (2 * confidence_delta_scaled);
}

/* Change the motion state and commit the change to AP. */
void body_detect_change_state(enum body_detect_states state, bool spoof)
{
	if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) && spoof_enable && !spoof)
		return;
	if (IS_ENABLED(CONFIG_GESTURE_HOST_DETECTION)) {
		struct ec_response_motion_sensor_data vector = {
			.flags = MOTIONSENSE_SENSOR_FLAG_BYPASS_FIFO,
			.activity_data = {
				.activity = MOTIONSENSE_ACTIVITY_BODY_DETECTION,
				.state = state,
			},
			.sensor_num = MOTION_SENSE_ACTIVITY_SENSOR_ID,
		};
		motion_sense_fifo_stage_data(&vector, NULL, 0,
					     __hw_clock_source_read());
		motion_sense_fifo_commit_data();
	}
	/* change the motion state */
	motion_state = state;
	if (state == BODY_DETECTION_ON_BODY) {
		/* reset time counting of stationary */
		stationary_timeframe = 0;
	}

	/* state changing log */
	print_body_detect_mode();

	if (IS_ENABLED(CONFIG_BODY_DETECTION_NOTIFY_MODE_CHANGE))
		host_set_single_event(EC_HOST_EVENT_BODY_DETECT_CHANGE);

	if (IS_ENABLED(CONFIG_BODY_DETECTION_NOTIFY_MKBP))
		mkbp_update_switches(EC_MKBP_FRONT_PROXIMITY, motion_state);

	hook_notify(HOOK_BODY_DETECT_CHANGE);
}

enum body_detect_states body_detect_get_state(void)
{
	return motion_state;
}

/* Determine window size for 1 second by sensor data rate. */
static void determine_window_size(int odr)
{
	window_size = odr / 1000;
	/* Normally, window_size should not exceed MAX_WINDOW_SIZE. */
	if (window_size > CONFIG_BODY_DETECTION_MAX_WINDOW_SIZE) {
		/* This will cause window size not enough for 1 second */
		CPRINTS("ODR exceeds CONFIG_BODY_DETECTION_MAX_WINDOW_SIZE");
		window_size = CONFIG_BODY_DETECTION_MAX_WINDOW_SIZE;
	}
}

/* Determine variance threshold scale by range. */
static void determine_threshold_scale(int range, int rms_noise,
				      int var_noise_factor, int var_threshold,
				      int confidence_delta)
{
	/*
	 * range:              g
	 * data_1g:            LSB/g
	 * data_1g / 9800:     LSB/(mm/s^2)
	 * (data_1g / 9800)^2: (LSB^2)/(mm^2/s^4), which number of
	 *                     var(sensor data) will represents 1 (mm^2/s^4)
	 * rms_noise:          ug
	 * var_noise:          mm^2/s^4
	 */
	const int data_1g = MOTION_SCALING_FACTOR / range;
	const int multiplier = POW2(data_1g);
	const int divisor = POW2(9800);

	/*
	 * We are measuring the var(X) + var(Y), so theoretically, the
	 * var(noise) should be 2 * rms_noise^2. However, in most case, on a
	 * very stationary plane, the average of var(noise) are less than 2 *
	 * rms_noise^2. We can multiply the rms_noise^2 with the
	 * CONFIG_BODY_DETECTION_VAR_NOISE_FACTOR / 100.
	 */
	const int var_noise = POW2((uint64_t)rms_noise) * var_noise_factor *
			      POW2(98) / 100 / POW2(10000);

	var_threshold_scaled =
		(uint64_t)(var_threshold + var_noise) * multiplier / divisor;
	confidence_delta_scaled =
		(uint64_t)confidence_delta * multiplier / divisor;
}

void body_detect_reset(void)
{
	int odr = body_sensor->drv->get_data_rate(body_sensor);
	int rms_noise = body_sensor->drv->get_rms_noise(body_sensor);
	int var_threshold, confidence_delta, var_noise_factor;

	if (motion_state == BODY_DETECTION_ON_BODY)
		stationary_timeframe = 0;
	else
		body_detect_change_state(BODY_DETECTION_ON_BODY, false);
	/*
	 * The sensor is suspended since its ODR is 0,
	 * there is no need to reset until sensor is up again
	 */
	if (odr == 0)
		return;

	/* If body detection params haven't been set, use the default ones. */
	if (!body_sensor->bd_params)
		body_sensor->bd_params = &default_body_detect_params;
	/*
	 * In case only some of the parameters have been specified use
	 * the default values for the rest of them.
	 */
	if (body_sensor->bd_params->var_noise_factor != 0)
		var_noise_factor = body_sensor->bd_params->var_noise_factor;
	else
		var_noise_factor = default_body_detect_params.var_noise_factor;

	if (body_sensor->bd_params->var_threshold != 0)
		var_threshold = body_sensor->bd_params->var_threshold;
	else
		var_threshold = default_body_detect_params.var_threshold;

	if (body_sensor->bd_params->confidence_delta != 0)
		confidence_delta = body_sensor->bd_params->confidence_delta;
	else
		confidence_delta = default_body_detect_params.confidence_delta;

	determine_window_size(odr);
	determine_threshold_scale(body_sensor->current_range, rms_noise,
				  var_noise_factor, var_threshold,
				  confidence_delta);
	/* initialize motion data and state */
	memset(data, 0, sizeof(data));
	history_idx = 0;
	history_initialized = 0;
}

void body_detect(void)
{
	uint64_t motion_var;
	int motion_confidence;

	if (!body_detect_enable)
		return;

	update_motion_variance();
	if (!history_initialized) {
		if (history_idx == window_size - 1)
			history_initialized = 1;
		return;
	}

	motion_var = get_motion_variance();
	motion_confidence = calculate_motion_confidence(motion_var);
	switch (motion_state) {
	case BODY_DETECTION_OFF_BODY:
		if (motion_confidence > CONFIG_BODY_DETECTION_ON_BODY_CON)
			body_detect_change_state(BODY_DETECTION_ON_BODY, false);
		break;
	case BODY_DETECTION_ON_BODY:
		stationary_timeframe += 1;
		/* confidence exceeds the limit, reset time counting */
		if (motion_confidence >= CONFIG_BODY_DETECTION_OFF_BODY_CON)
			stationary_timeframe = 0;
		/* if no motion for enough time, change state to off_body */
		if (stationary_timeframe >=
		    CONFIG_BODY_DETECTION_STATIONARY_DURATION * window_size)
			body_detect_change_state(BODY_DETECTION_OFF_BODY,
						 false);
		break;
	}
}

void body_detect_set_enable(int enable)
{
	body_detect_enable = enable;
	body_detect_change_state(BODY_DETECTION_ON_BODY, false);
}

int body_detect_get_enable(void)
{
	return body_detect_enable;
}

#ifdef CONFIG_ACCEL_SPOOF_MODE
void body_detect_set_spoof(int enable)
{
	spoof_enable = enable;
	/* After disabling spoof mode, commit current state. */
	if (!enable)
		body_detect_change_state(motion_state, false);
}

bool body_detect_get_spoof(void)
{
	return spoof_enable;
}

static int command_setbodydetectionmode(int argc, const char **argv)
{
	if (argc == 1) {
		print_body_detect_mode();
		return EC_SUCCESS;
	}

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	/* |+1| to also make sure the strings the same length. */
	if (strncmp(argv[1], "on", strlen("on") + 1) == 0) {
		body_detect_change_state(BODY_DETECTION_ON_BODY, true);
		spoof_enable = true;
	} else if (strncmp(argv[1], "off", strlen("off") + 1) == 0) {
		body_detect_change_state(BODY_DETECTION_OFF_BODY, true);
		spoof_enable = true;
	} else if (strncmp(argv[1], "reset", strlen("reset") + 1) == 0) {
		body_detect_reset();
		/*
		 * Don't call body_detect_set_spoof(), since
		 * body_detect_change_state() was already called by
		 * body_detect_reset().
		 */
		spoof_enable = false;
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(
	bodydetectmode, command_setbodydetectionmode, "[on | off | reset]",
	"Manually force body detect mode to on (body), off (body) or reset.");
#endif /* CONFIG_ACCEL_SPOOF_MODE */
