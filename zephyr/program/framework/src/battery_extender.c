/*
 * Copyright 2024 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "board_host_command.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "timer.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

#define DAY (24 * HOUR)
#define TIMES2DAYS(d) (d / DAY)
#define TIMES2MINUTES(m) (m / MINUTE)
#define TIMES2SECOND(s) (s / SECOND)

#define BATTERY_EXTENDER_STAGE1_VOLTAGE(v) (v * 97 / 100)
#define BATTERY_EXTENDER_STAGE2_VOLTAGE(v) (v * 96 / 100)

enum battery_extender_stage_t {
	BATT_EXTENDER_STAGE_0,
	BATT_EXTENDER_STAGE_1,
	BATT_EXTENDER_STAGE_2,
};

static bool batt_extender_disable;
static bool batt_extender_timer_is_reset;
static bool reset_timer_is_reset;
static bool manual_test_enable;
static uint64_t battery_extender_days = 5;
static uint64_t battery_extender_reset_minutes = 30;
static int stage;
static timestamp_t batt_extender_deadline;
static timestamp_t reset_deadline;

static bool check_battery_extender_reset_timer(void)
{
	static bool timer_initial, pre_manual_test, pre_batt_extender_disable;
	static uint64_t pre_batt_extender_days, pre_batt_extender_reset;
	timestamp_t battery_extender_reset_timer;

	/* Initial the timer */
	if (!timer_initial) {
		timer_initial = true;
		reset_timer_is_reset = false;
		pre_manual_test = manual_test_enable;
		pre_batt_extender_disable = batt_extender_disable;
		pre_batt_extender_days = battery_extender_days;
		pre_batt_extender_reset = battery_extender_reset_minutes;
		return true;
	}

	/* Reload the timer if the manual test status is changed */
	if (pre_manual_test != manual_test_enable) {
		reset_timer_is_reset = false;
		pre_manual_test = manual_test_enable;
		return true;
	}

	/* Reload the timer if the battery extender days is changed */
	if (pre_batt_extender_days != battery_extender_days) {
		reset_timer_is_reset = false;
		pre_batt_extender_days = battery_extender_days;

		/* Don't need to reset the timer when battery extender is on */
		if (stage == BATT_EXTENDER_STAGE_0)
			return true;
		else
			return false;
	}

	/* Reload the timer if the battery extender status is changed */
	if (pre_batt_extender_disable != batt_extender_disable) {
		reset_timer_is_reset = false;
		pre_batt_extender_disable = batt_extender_disable;
		return true;
	}

	/* Reload the reset timer if the battery extender reset minutes is changed */
	if (pre_batt_extender_reset != battery_extender_reset_minutes) {
		reset_timer_is_reset = false;
		pre_batt_extender_reset = battery_extender_reset_minutes;
	}

	/* Do not run the reset timer when battery extender feature is disabled */
	if (batt_extender_disable)
		return false;

	/**
	 * Only reset the timer when the adapter is disconnect and system runs
	 * at the S0/S0ix state
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) || extpower_is_present()) {
		reset_timer_is_reset = false;
		return false;
	}

	/* Set deadline once time */
	if (!reset_timer_is_reset) {
		if (manual_test_enable) {
			reset_deadline.val =
				get_time().val + battery_extender_reset_minutes * SECOND;
		} else {
			reset_deadline.val =
				get_time().val + battery_extender_reset_minutes * MINUTE;
		}
		reset_timer_is_reset = true;
	}

	battery_extender_reset_timer = get_time();
	if (timestamp_expired(reset_deadline, &battery_extender_reset_timer))
		return true;
	else
		return false;
}

void battery_extender(void)
{
	timestamp_t battery_extender_timer = get_time();

	batt_extender_timer_is_reset = check_battery_extender_reset_timer();

	if (batt_extender_disable) {
		stage = BATT_EXTENDER_STAGE_0;
		return;
	}

	if (!batt_extender_timer_is_reset &&
		timestamp_expired(batt_extender_deadline, &battery_extender_timer)) {
		if (stage == BATT_EXTENDER_STAGE_0) {
			stage = BATT_EXTENDER_STAGE_1;
			CPRINTS("Battery extender in stage 1");
			if (manual_test_enable)
				batt_extender_deadline.val = get_time().val + 2 * MINUTE;
			else
				batt_extender_deadline.val = get_time().val + 2 * DAY;
		} else if (stage == BATT_EXTENDER_STAGE_1) {
			stage = BATT_EXTENDER_STAGE_2;
			CPRINTS("Battery extender in stage 2");
		}
	} else if (batt_extender_timer_is_reset) {
		stage = BATT_EXTENDER_STAGE_0;
		if (manual_test_enable) {
			batt_extender_deadline.val =
				get_time().val + battery_extender_days * MINUTE;
		} else {
			batt_extender_deadline.val =
				get_time().val + battery_extender_days * DAY;
		}
	}
}

int battery_extender_stage_voltage(uint16_t volatge)
{
	if (stage == BATT_EXTENDER_STAGE_1)
		return BATTERY_EXTENDER_STAGE1_VOLTAGE(volatge);
	else if (stage == BATT_EXTENDER_STAGE_2)
		return BATTERY_EXTENDER_STAGE2_VOLTAGE(volatge);
	else
		return 0;
}

/* Host command for battery extender feature */
static enum ec_status battery_extender_hc(struct host_cmd_handler_args *args)
{
	const struct ec_params_battery_extender *p = args->params;
	struct ec_response_battery_extender *r = args->response;
	uint64_t timestamps, temp;

	if (p->cmd == BATT_EXTENDER_WRITE_CMD) {
		if (p->disable != batt_extender_disable)
			batt_extender_disable = p->disable;

		if (p->manual != manual_test_enable)
			manual_test_enable = p->manual;

		if ((p->days != battery_extender_days) &&
			(p->days >= 1 && p->days <= 99))
			battery_extender_days = p->days;

		if ((p->minutes != battery_extender_reset_minutes) &&
			(p->minutes >= 1 && p->minutes <= 9999))
			battery_extender_reset_minutes = p->minutes;

		return EC_SUCCESS;
	} else if (p->cmd == BATT_EXTENDER_READ_CMD) {
		/* return the current stage for debugging */
		r->current_stage = stage;

		if (!batt_extender_timer_is_reset && !batt_extender_disable) {
			if (manual_test_enable) {
				timestamps =
				  batt_extender_deadline.val - (battery_extender_days * MINUTE);
				if (stage >= BATT_EXTENDER_STAGE_1)
					timestamps -= 2 * MINUTE;
				temp = TIMES2MINUTES((get_time().val - timestamps));
			} else {
				timestamps =
				  batt_extender_deadline.val - (battery_extender_days * DAY);
				if (stage >= BATT_EXTENDER_STAGE_1)
					timestamps -= 2 * DAY;
				temp = TIMES2DAYS((get_time().val - timestamps));
			}

			r->days = (uint16_t)temp;
		} else
			r->days = 0;

		if (reset_timer_is_reset) {
			if (manual_test_enable) {
				timestamps =
				  reset_deadline.val - (battery_extender_reset_minutes * SECOND);
				temp = TIMES2SECOND((get_time().val - timestamps));
			} else {
				timestamps =
				  reset_deadline.val - (battery_extender_reset_minutes * MINUTE);
				temp = TIMES2MINUTES((get_time().val - timestamps));
			}

			r->minutes = (uint16_t)temp;
		} else
			r->minutes = 0;

		args->response_size = sizeof(*r);
		return EC_SUCCESS;
	} else
		return EC_ERROR_PARAM1;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_EXTENDER, battery_extender_hc, EC_VER_MASK(0));

/* Console command for battery extender manual control */
static int cmd_batt_extender(int argc, const char **argv)
{
	char *e;
	int disable, days, minutes;
	uint64_t timestamps = battery_extender_days * (manual_test_enable ? MINUTE : DAY);
	uint64_t reset_timestamps =
		battery_extender_reset_minutes * (manual_test_enable ? SECOND : MINUTE);

	if (argc >= 2) {
		if (!strncmp(argv[1], "en", 2) || !strncmp(argv[1], "dis", 3)) {
			if (!parse_bool(argv[1], &disable))
				return EC_ERROR_PARAM1;

			batt_extender_disable = !disable;
			CPRINTS("battery extender %s",
				disable ? "enables" : "disables");
		} else if (!strncmp(argv[1], "manual", 6)) {
			if (argc < 3)
				return EC_ERROR_PARAM2;

			manual_test_enable = strtoi(argv[2], &e, 0);
			CPRINTS("manual test %s",
				manual_test_enable ? "enables" : "disables");
		} else if (!strncmp(argv[1], "days", 4)) {
			if (argc < 3)
				return EC_ERROR_PARAM2;

			days = strtoi(argv[2], &e, 0);
			if (days < 1 || days > 99)
				return EC_ERROR_PARAM2;

			battery_extender_days = days;
			CPRINTS("update battery extender days %lld",
				battery_extender_days);
		} else if (!strncmp(argv[1], "reset", 5)) {
			if (argc < 3)
				return EC_ERROR_PARAM2;

			minutes = strtoi(argv[2], &e, 0);
			if (minutes < 1 || minutes > 9999)
				return EC_ERROR_PARAM2;

			battery_extender_reset_minutes = minutes;
			CPRINTS("update battery extender reset minutes %lld",
				battery_extender_reset_minutes);
		} else
			return EC_ERROR_PARAM_COUNT;
	} else {
		CPRINTS("Battery extender %sable", batt_extender_disable ? "dis" : "en");
		CPRINTS("\tCurrent stage:%d", stage);
		CPRINTS("\tManual %sable", manual_test_enable ? "en" : "dis");
		CPRINTS("\tBattery extender timer %sable",
			(batt_extender_timer_is_reset || batt_extender_disable) ? "dis" : "en");
		if (!batt_extender_timer_is_reset && !batt_extender_disable) {
			if (stage >= BATT_EXTENDER_STAGE_1)
				timestamps += 2 * (manual_test_enable ? MINUTE : DAY);
			CPRINTS("\t - Timer:%lld usec",
				get_time().val - (batt_extender_deadline.val - timestamps));
		}
		CPRINTS("\tBattery extender reset timer %sable",
			reset_timer_is_reset ? "en" : "dis");
		if (reset_timer_is_reset) {
			CPRINTS("\t - Timer:%lld usec",
				get_time().val - (reset_deadline.val - reset_timestamps));
		}
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battextender, cmd_batt_extender,
			"[enable/disable/days/reset/manual][days:1-99][reset:1-9999][manual:1/0]",
			"battery extender control");
