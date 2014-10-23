/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Board specific gesture recognition */

#include "accelgyro.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "gesture.h"
#include "lid_switch.h"
#include "lightbar.h"
#include "motion_sense.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_GESTURE, outstr)
#define CPRINTS(format, args...) cprints(CC_GESTURE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_GESTURE, format, ## args)

/* Output datarate for tap sensor (in milli-Hz) */
#define TAP_ODR (1000000 / CONFIG_GESTURE_SAMPLING_INTERVAL_MS)

/*
 * Double tap detection parameters
 * Double tap works by looking for two isolated Z-axis accelerometer impulses
 * preceded and followed by relatively calm periods of accelerometer motion.
 *
 * Define an outer and inner window. The inner window specifies how
 * long the tap impulse is expected to last. The outer window specifies the
 * period before the initial tap impluse and after the final tap impulse for
 * which to check for relatively calm periods. In between the two impulses
 * there is a minimum and maximum interstice time allowed.
 */
#define OUTER_WINDOW_T 200
#define INNER_WINDOW_T 30
#define MIN_INTERSTICE_T 120
#define MAX_INTERSTICE_T 500

#define OUTER_WINDOW \
	(CONFIG_GESTURE_TAP_OUTER_WINDOW_T / \
	 CONFIG_GESTURE_SAMPLING_INTERVAL_MS)
#define INNER_WINDOW \
	(CONFIG_GESTURE_TAP_INNER_WINDOW_T / \
	 CONFIG_GESTURE_SAMPLING_INTERVAL_MS)
#define MIN_INTERSTICE \
	(CONFIG_GESTURE_TAP_MIN_INTERSTICE_T / \
	 CONFIG_GESTURE_SAMPLING_INTERVAL_MS)
#define MAX_INTERSTICE \
	(CONFIG_GESTURE_TAP_MAX_INTERSTICE_T / \
	 CONFIG_GESTURE_SAMPLING_INTERVAL_MS)
#define MAX_WINDOW OUTER_WINDOW

/* State machine states for detecting double tap */
enum tap_states {
	/* Look for calm before the storm */
	TAP_IDLE,
	/* Record first Z impulse */
	TAP_IMPULSE_1,

	/* Eye of the storm, expect Z motion to drop and then suddenly spike */
	TAP_INTERSTICE_DROP,
	TAP_INTERSTICE_RISE,

	/* Record second Z impulse */
	TAP_IMPULSE_2,
	/* Should be quiet after the storm */
	TAP_AFTER_EVENT
};

/* Tap sensor to use */
static struct motion_sensor_t *sensor =
&motion_sensors[CONFIG_SENSOR_BATTERY_TAP];

/* Tap state information */
static int history_z[MAX_WINDOW];  /* Changes in Z */
static int history_xy[MAX_WINDOW]; /* Changes in X and Y */
static int state, history_idx;
static int history_initialized;
static int tap_debug;

/* Tap detection flag */
static int tap_detection;

/*
 * TODO(crosbug.com/p/33102): Cleanup this function: break into multiple
 * functions and generalize so it can be used for other boards.
 */
static int gesture_tap_for_battery(void)
{
	/* Current and previous accel x,y,z */
	int x, y, z;
	static int x_p, y_p, z_p;

	/* Number of iterations in this state */
	static int state_cnt;

	/*
	 * Running sums of data diffs for inner and outer windows.
	 * Z data kept seperate from X and Y data
	 */
	static int sum_z_inner, sum_z_outer, sum_xy_inner, sum_xy_outer;

	/* Total variation in each signal, normalized for window size */
	int delta_z_outer, delta_z_inner, delta_xy_outer, delta_xy_inner;

	/* Max variation seen during tap event and state cnts since max */
	static int delta_z_inner_max;
	static int cnts_since_max;

	/* Interstice Z motion thresholds */
	static int z_drop_thresh, z_rise_thresh;

	int history_idx_inner, state_p;
	int ret = 0;

	/* Get data */
	x = sensor->xyz[0];
	y = sensor->xyz[1];
	z = sensor->xyz[2];

	/*
	 * Calculate history of change in Z sensor and keeping
	 * running sums for the past.
	 */
	history_idx_inner = history_idx - INNER_WINDOW;
	if (history_idx_inner < 0)
		history_idx_inner += MAX_WINDOW;
	sum_z_inner -= history_z[history_idx_inner];
	sum_z_outer -= history_z[history_idx];
	history_z[history_idx] = ABS(z - z_p);
	sum_z_inner += history_z[history_idx];
	sum_z_outer += history_z[history_idx];

	/*
	 * Calculate history of change in X and Y sensors combined
	 * and keep a running sum of the change over the past.
	 */
	sum_xy_inner -= history_xy[history_idx_inner];
	sum_xy_outer -= history_xy[history_idx];
	history_xy[history_idx] = ABS(x - x_p) + ABS(y - y_p);
	sum_xy_inner += history_xy[history_idx];
	sum_xy_outer += history_xy[history_idx];

	/* Increment history index */
	history_idx = (history_idx == MAX_WINDOW - 1) ? 0 : (history_idx + 1);

	/* Store previous X, Y, Z data */
	x_p = x;
	y_p = y;
	z_p = z;

	/* Ignore data until we fill history buffer and wrap around */
	if (history_idx == 0)
		history_initialized = 1;
	if (history_initialized == 0)
		return 0;

	/*
	 * Normalize data based on window size and isolate outer and inner
	 * window data.
	 */
	delta_z_outer = (sum_z_outer - sum_z_inner) * 1000 /
			(OUTER_WINDOW - INNER_WINDOW);
	delta_z_inner = sum_z_inner * 1000 / INNER_WINDOW;
	delta_xy_outer = (sum_xy_outer - sum_xy_inner) * 1000 /
			(OUTER_WINDOW - INNER_WINDOW);
	delta_xy_inner = sum_xy_inner * 1000 / INNER_WINDOW;

	state_cnt++;
	state_p = state;

	switch (state) {
	case TAP_IDLE:
		/* Look for a sudden increase in Z movement */
		if (delta_z_inner > 13 * delta_z_outer &&
		    delta_z_inner > 1 * delta_xy_inner) {
			delta_z_inner_max = delta_z_inner;
			state_cnt = TAP_IDLE;
			state = TAP_IMPULSE_1;
		}
		break;

	case TAP_IMPULSE_1:
		/* Find the peak inner window of Z movement */
		if (delta_z_inner > delta_z_inner_max) {
			delta_z_inner_max = delta_z_inner;
			cnts_since_max = state_cnt;
		}

		/* After inner window has passed, move to next state */
		if (state_cnt >= INNER_WINDOW) {
			state = TAP_INTERSTICE_DROP;
			z_drop_thresh = delta_z_inner_max / 12;
			z_rise_thresh = delta_z_inner_max / 3;
			state_cnt += INNER_WINDOW - cnts_since_max;
		}
		break;

	case TAP_INTERSTICE_DROP:
		/* Check for z motion to go back down first */
		if (delta_z_inner < z_drop_thresh)
			state = TAP_INTERSTICE_RISE;

		if (state_cnt > MAX_INTERSTICE)
			state = TAP_IDLE;

		break;

	case TAP_INTERSTICE_RISE:
		/* Then, check for z motion to go back up */
		if (delta_z_inner > z_rise_thresh) {
			if (state_cnt < MIN_INTERSTICE) {
				state = TAP_IDLE;
			} else {
				delta_z_inner_max = delta_z_inner;
				state_cnt = 0;
				state = TAP_IMPULSE_2;
			}
		}

		if (state_cnt > MAX_INTERSTICE)
			state = TAP_IDLE;
		break;

	case TAP_IMPULSE_2:
		/* Find the peak inner window of Z movement */
		if (delta_z_inner > delta_z_inner_max) {
			delta_z_inner_max = delta_z_inner;
			cnts_since_max = state_cnt;
		}

		/* After inner window has passed, move to next state */
		if (state_cnt >= INNER_WINDOW) {
			state = TAP_AFTER_EVENT;
			state_cnt += INNER_WINDOW - cnts_since_max;
		}

	case TAP_AFTER_EVENT:
		/* Check for small Z movement after the event */
		if (state_cnt < OUTER_WINDOW)
			break;

		if (2 * delta_z_inner_max > 3 * delta_z_outer &&
		    delta_z_outer > 1 * delta_xy_outer)
			ret = 1;

		state = TAP_IDLE;
		break;
	}

	/* On state transitions, print debug info */
	if (state != state_p && tap_debug) {
		/* make sure we don't divide by 0 */
		if (delta_z_outer == 0 || delta_xy_inner == 0)
			CPRINTS("tap st %d->%d, error div by 0",
				state_p, state);
		else
			CPRINTS("tap st %d->%d, st_cnt %-3d",
				state_p, state, state_cnt);
			CPRINTS("Z_in:Z_out %-3d, Z_in:XY_in %-3d",
				delta_z_inner / delta_z_outer,
				delta_z_inner / delta_xy_inner);
			CPRINTS("dZ_in %-8.3d, dZ_in_max %-8.3d, dZ_out %-8.3d",
				delta_z_inner, delta_z_inner_max,
				delta_z_outer);
	}

	return ret;
}

static void gesture_chipset_resume(void)
{
	/* disable tap detection */
	tap_detection = 0;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, gesture_chipset_resume,
	     GESTURE_HOOK_PRIO);

static void gesture_chipset_suspend(void)
{
	/* Set ODR to desired value */
	sensor->drv->set_data_rate(sensor, TAP_ODR, 1);

	/*
	 * Clear tap init and history index so that we have to
	 * record a whole new set of data, and enable tap detection
	 */
	history_initialized = 0;
	state = 0;
	history_idx = 0;
	tap_detection = 1;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, gesture_chipset_suspend,
	     GESTURE_HOOK_PRIO);

void gesture_calc(void)
{
	/* Only check for gesture if lid is closed and tap detection is on */
	if (!tap_detection || lid_is_open())
		return;

	if (gesture_tap_for_battery()) {
		CPRINTS("Double Tap!");
		lightbar_sequence(LIGHTBAR_TAP);

		/* Don't need to run motion sense task for a while */
		task_wait_event(500 * MSEC);
	}
}

/*****************************************************************************/
/* Console commands */
static int command_tap_info(int argc, char **argv)
{
	int odr, val;

	ccprintf("tap:   %s\n", (tap_detection && !lid_is_open()) ?
					"on" : "off");

	if (argc > 1) {
		if (!parse_bool(argv[1], &val))
			return EC_ERROR_PARAM1;
		tap_debug = val;
	}

	ccprintf("debug: %s\n", tap_debug ? "on" : "off");
	sensor->drv->get_data_rate(sensor, &odr);
	ccprintf("odr:   %d\n", odr);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tapinfo, command_tap_info,
			"debug on/off",
			"Print tap information", NULL);

