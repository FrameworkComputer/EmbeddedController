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
#include "hooks.h"



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
static uint64_t battery_extender_trigger = 5*DAY;
static uint64_t battery_extender_reset = 30*MINUTE;
static int stage;
static timestamp_t batt_extender_deadline;
static timestamp_t batt_extender_deadline_stage2;

static timestamp_t reset_deadline;


static void extender_init(void)
{
	timestamp_t now = get_time();

	batt_extender_deadline.val =
			now.val + battery_extender_trigger;
	batt_extender_deadline_stage2.val =
			now.val + battery_extender_trigger + 2*DAY;
	reset_deadline.val = 0; /* not active */
}
DECLARE_HOOK(HOOK_INIT, extender_init, HOOK_PRIO_DEFAULT);


void battery_extender(void)
{
	timestamp_t now = get_time();

	if (batt_extender_disable) {
		stage = BATT_EXTENDER_STAGE_0;
		reset_deadline.val = 0;
		batt_extender_deadline.val = 0;
		batt_extender_deadline_stage2.val = 0;
		return;
	}

	if (extpower_is_present()) {
		/* just keep pushing the reset timer into the future if we are on AC */
		reset_deadline.val = now.val + battery_extender_reset;
	}

	if (reset_deadline.val &&
			timestamp_expired(reset_deadline, &now)) {
		reset_deadline.val = 0;

		stage = BATT_EXTENDER_STAGE_0;
		batt_extender_deadline.val =
			now.val + battery_extender_trigger;
		batt_extender_deadline_stage2.val =
				now.val + battery_extender_trigger + 2*DAY;
		battery_sustainer_set(-1, -1);
		set_chg_ctrl_mode(CHARGE_CONTROL_NORMAL);
	}

	if (batt_extender_deadline_stage2.val &&
			timestamp_expired(batt_extender_deadline_stage2, &now)) {
		batt_extender_deadline_stage2.val = 0;
		stage = BATT_EXTENDER_STAGE_2;
		battery_sustainer_set(85, 87);
	}

	else if (batt_extender_deadline.val &&
			timestamp_expired(batt_extender_deadline, &now)) {
		batt_extender_deadline.val = 0;
		stage = BATT_EXTENDER_STAGE_1;
		battery_sustainer_set(90, 95);
	}
}
DECLARE_HOOK(HOOK_SECOND, battery_extender, HOOK_PRIO_DEFAULT);


/* Host command for battery extender feature */
static enum ec_status battery_extender_hc(struct host_cmd_handler_args *args)
{
	const struct ec_params_battery_extender *p = args->params;
	struct ec_response_battery_extender *r = args->response;
	timestamp_t now = get_time();

	if (p->cmd == BATT_EXTENDER_WRITE_CMD) {

		if ((p->trigger_days != TIMES2DAYS(battery_extender_trigger)) &&
			(p->trigger_days >= 1 && p->trigger_days <= 99)) {
			battery_extender_trigger = p->trigger_days * DAY;
			if (battery_extender_trigger != 0) {
				batt_extender_deadline.val =
						now.val + battery_extender_trigger;
				batt_extender_deadline_stage2.val =
						now.val + battery_extender_trigger + 2*DAY;
			} else {
				batt_extender_deadline.val = 0;
				batt_extender_deadline_stage2.val = 0;
			}
		}

		if ((p->reset_minutes != TIMES2MINUTES(battery_extender_reset)) &&
			(p->reset_minutes >= 1 && p->reset_minutes <= 9999)) {
			battery_extender_reset = p->reset_minutes * MINUTE;
			if (battery_extender_reset) {
				reset_deadline.val =
					now.val + battery_extender_reset;
			} else {
				reset_deadline.val = 0;
			}
		}

		if (batt_extender_disable != p->disable) {
			batt_extender_disable = p->disable;
			if (batt_extender_disable) {
				battery_sustainer_set(-1, -1);
				set_chg_ctrl_mode(CHARGE_CONTROL_NORMAL);
				stage = BATT_EXTENDER_STAGE_0;
			}
		}
		return EC_SUCCESS;
	} else if (p->cmd == BATT_EXTENDER_READ_CMD) {
		/* return the current stage for debugging */
		r->current_stage = stage;
		r->disable = batt_extender_disable;

		if (!timestamp_expired(batt_extender_deadline, &now)) {
			r->trigger_timedelta = batt_extender_deadline.val - now.val;
		} else
			r->trigger_timedelta = 0;

		r->trigger_days = (uint16_t)(battery_extender_trigger/DAY);


		if (!timestamp_expired(reset_deadline, &now)) {
			r->reset_timedelta = reset_deadline.val - now.val;
		} else
			r->reset_timedelta = 0;

		r->reset_minutes = (uint16_t)(battery_extender_reset/MINUTE);

		args->response_size = sizeof(*r);
		return EC_SUCCESS;
	} else
		return EC_ERROR_PARAM1;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_EXTENDER, battery_extender_hc, EC_VER_MASK(0));

static uint64_t cmd_parse_timestamp(int argc, const char **argv)
{
	uint64_t time_val = 0;
	char *e;

	if (argc >= 3) {
		time_val = strtoi(argv[2], &e, 0);
		if (!strncmp(argv[3], "s", 1)) {
			time_val *= SECOND;
		} else	if (!strncmp(argv[3], "m", 1)) {
			time_val *= MINUTE;
		} else	if (!strncmp(argv[3], "h", 1)) {
			time_val *= HOUR;
		} else if (!strncmp(argv[3], "d", 1)) {
			time_val *= DAY;
		} else {
			CPRINTF("invalid option for time scale: %s. Valid options: [s,h,d]\n",
				argv[3]);
			return EC_ERROR_PARAM3;
		}
		return time_val;
	}

	CPRINTF("invalid parameters:\n");
	return 0;
}

static void print_time_offset(uint64_t t_end, uint64_t t_start)
{
	uint64_t t = t_end - t_start;
	uint64_t d = TIMES2DAYS(t);
	uint64_t h = (t % DAY) / HOUR;
	uint64_t m = (t % HOUR) / MINUTE;
	uint64_t s = (t % MINUTE) / SECOND;

	if (t_end < t_start) {
		CPRINTF("Expired\n");
	} else {
		CPRINTF("%lldD:%lldH:%lldM:%lldS\n", d, h, m, s);
	}
}

/* Console command for battery extender manual control */
static int cmd_batt_extender(int argc, const char **argv)
{
	int disable;
	timestamp_t now = get_time();

	if (argc >= 2) {
		if (!strncmp(argv[1], "en", 2) || !strncmp(argv[1], "dis", 3)) {
			if (!parse_bool(argv[1], &disable))
				return EC_ERROR_PARAM1;

			batt_extender_disable = !disable;
			CPRINTS("battery extender %s",
				disable ? "enabled" : "disabled");
			if (batt_extender_disable) {
				battery_sustainer_set(-1, -1);
				set_chg_ctrl_mode(CHARGE_CONTROL_NORMAL);
				stage = BATT_EXTENDER_STAGE_0;
			} else {
				if (battery_extender_reset) {
					reset_deadline.val =
						now.val + battery_extender_reset;
				}
				if (battery_extender_trigger != 0) {
					batt_extender_deadline.val =
							now.val + battery_extender_trigger;
					batt_extender_deadline_stage2.val =
							now.val + battery_extender_trigger + 2*DAY;
				}
			}
		} else if (!strncmp(argv[1], "timeext2", 8)) {
			batt_extender_deadline_stage2.val =
				now.val + cmd_parse_timestamp(argc, argv);
		} else if (!strncmp(argv[1], "timeext", 7)) {
			batt_extender_deadline.val = now.val + cmd_parse_timestamp(argc, argv);
		} else if (!strncmp(argv[1], "timerst", 7)) {
			reset_deadline.val = now.val + cmd_parse_timestamp(argc, argv);
		} else if (!strncmp(argv[1], "trigger", 7)) {
			if (argc < 3)
				return EC_ERROR_PARAM2;

			battery_extender_trigger = cmd_parse_timestamp(argc, argv);
			CPRINTF("update battery extender trigger ");
			print_time_offset(battery_extender_trigger, 0);
		} else if (!strncmp(argv[1], "reset", 5)) {
			if (argc < 3)
				return EC_ERROR_PARAM2;

			battery_extender_reset =  cmd_parse_timestamp(argc, argv);
			CPRINTF("update battery extender reset ");
			print_time_offset(battery_extender_reset, 0);
		} else
			return EC_ERROR_PARAM_COUNT;
	} else {
		CPRINTF("Battery extender %sabled\n", batt_extender_disable ? "dis" : "en");
		CPRINTF("\tTrigger:");
		print_time_offset(battery_extender_trigger, 0);
		CPRINTF("\tReset:");
		print_time_offset(battery_extender_reset, 0);

		CPRINTF("\tCurrent stage:%d\n", stage);
		CPRINTF("\tBattery extender timer\n");
		if (batt_extender_deadline.val) {
			CPRINTF("\t - Stage 1 expires in: ");
			print_time_offset(batt_extender_deadline.val, now.val);
		}
		if (batt_extender_deadline_stage2.val) {
			CPRINTF("\t - Stage 2 expires in: ");
			print_time_offset(batt_extender_deadline_stage2.val, now.val);
		}
		CPRINTF("\tBattery extender reset timer %sable\n",
			reset_deadline.val ? "en" : "dis");
		if (reset_deadline.val) {
			CPRINTF("\t - expires in: ");
			print_time_offset(reset_deadline.val, now.val);
		}
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battextender, cmd_batt_extender,
			"[enable/disable/days/reset/manual][days:1-99][reset:1-9999][manual:1/0]",
			"battery extender control");
