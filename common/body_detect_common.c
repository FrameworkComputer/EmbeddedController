/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "body_detection.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "motion_sense_fifo.h"
#include "timer.h"

/* Support V1 algorithm */
#ifndef CONFIG_BODY_DETECTION_ALOGIRTHM_V1
static
#endif
	int onbody_stationary_timeframe;

/* Support V2 algorithm */
#ifndef CONFIG_BODY_DETECTION_ALOGIRTHM_V2
static
#endif
	uint64_t onbody_lasttime;

bool spoof_enable;

/* Clock */
test_export_static timestamp_t (*get_time_ptr)(void) = get_time;

uint64_t onbody_get_curtime(void)
{
	timestamp_t time = get_time_ptr();

	return time.val;
}

static void body_detect_send_host_event(enum body_detect_states state)
{
	if (!IS_ENABLED(CONFIG_GESTURE_HOST_DETECTION)) {
		/* Not configured for host events. */
		return;
	}
	if (!IS_ENABLED(CONFIG_ACCEL_FIFO)) {
		/* Motion sense FIFO isn't enabled */
		return;
	}
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

void body_detect_change_state(enum body_detect_states state, bool spoof)
{
	if (IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) && spoof_enable && !spoof) {
		return;
	}
	body_detect_send_host_event(state);

	if (state == BODY_DETECTION_ON_BODY) {
		/* reset time counting of stationary */
		onbody_lasttime = onbody_get_curtime();
		onbody_stationary_timeframe = 0;
	}

	body_detect_change_state_extern(state);
}

#ifdef CONFIG_ACCEL_SPOOF_MODE
void body_detect_set_spoof(int enable)
{
	spoof_enable = enable;
	/* After disabling spoof mode, commit current state. */
	if (!enable) {
		body_detect_change_state(body_detect_get_state(), false);
	}
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
