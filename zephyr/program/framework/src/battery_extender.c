/*
 * Copyright 2024 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "board_host_command.h"
#include "board_charger.h"
#include "battery_fuel_gauge.h"
#include "board_function.h"
#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "customized_shared_memory.h"
#include "ec_commands.h"
#include "extpower.h"
#include "factory.h"
#include "host_command.h"
#include "system.h"
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


/*
 * When in stage_0, will not control the charger
 * in stage_1 will set charger mode to idle and keeps battery at 90%~95%
 * in stage_2 will set charger mode to idle and keeps battery at 85%~87%
 * if user setting value lower than stage_1 or 2 will use the setting value
 * ex: if user set charging_maximum_level to 70% battery will keeps at 65%~70% (min=max - 5)
 */
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
static int sustainer_lower = 100;
static int sustainer_upper = 100;

static uint8_t charging_maximum_level = EC_CHARGE_LIMIT_RESTORE;
static uint8_t old_charger_limit;

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

int charger_sustainer_percentage(void)
{
	if (!charging_maximum_level)
		return 100;
	return charging_maximum_level;
}

void charger_sustainer_reset(void)
{
	old_charger_limit = 0;
	battery_sustainer_set(-1, -1);
	set_chg_ctrl_mode(CHARGE_CONTROL_NORMAL);
}

static void battery_percentage_control(void)
{
	if (charging_maximum_level == EC_CHARGE_LIMIT_RESTORE) {
		system_get_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, &charging_maximum_level);
		if (charging_maximum_level & CHG_LIMIT_OVERRIDE)
			charging_maximum_level = charging_maximum_level & 0x64;
	}

	if (charging_maximum_level & CHG_LIMIT_OVERRIDE) {
		charger_sustainer_reset();
		return;
	}

	if (!charging_maximum_level ||
		charging_maximum_level == 100) {
		if (stage == BATT_EXTENDER_STAGE_0)
			charger_sustainer_reset();
		return;
	}

	if (old_charger_limit != charging_maximum_level) {
		old_charger_limit = charging_maximum_level;
		battery_sustainer_set(MAX(20, (charging_maximum_level - 5)),
			MAX(20, charging_maximum_level));
	}
}

void battery_extender(void)
{
	timestamp_t now = get_time();

	if (stage == BATT_EXTENDER_STAGE_0)
		battery_percentage_control();

	/* don't runnig extender when unit in factory mode */
	if (batt_extender_disable || factory_status()) {
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

	if (sustainer_upper != charger_sustainer_percentage()) {
		sustainer_upper = charger_sustainer_percentage();
		sustainer_lower = sustainer_upper - 5;
		/* if we already trigger stage, set 5 SECOND to update the sustainer again */
		if (stage == BATT_EXTENDER_STAGE_1) {
			batt_extender_deadline.val = now.val + 5 * SECOND;
		} else if (stage == BATT_EXTENDER_STAGE_2)
			batt_extender_deadline_stage2.val = now.val + 5 * SECOND;
	}

	if (reset_deadline.val &&
			timestamp_expired(reset_deadline, &now)) {
		reset_deadline.val = 0;

		stage = BATT_EXTENDER_STAGE_0;
		batt_extender_deadline.val =
			now.val + battery_extender_trigger;
		batt_extender_deadline_stage2.val =
				now.val + battery_extender_trigger + 2*DAY;
		/* if charger limit has been set don't clear sustainer and charger mode */
		if (charger_sustainer_percentage() == 100) {
			charger_sustainer_reset();
		}
	}

	if (batt_extender_deadline_stage2.val &&
			timestamp_expired(batt_extender_deadline_stage2, &now)) {
		batt_extender_deadline_stage2.val = 0;
		stage = BATT_EXTENDER_STAGE_2;
		battery_sustainer_set(MIN(85, sustainer_lower), MIN(87, sustainer_upper));
	} else if (batt_extender_deadline.val &&
			timestamp_expired(batt_extender_deadline, &now)) {
		batt_extender_deadline.val = 0;
		stage = BATT_EXTENDER_STAGE_1;
		battery_sustainer_set(MIN(90, sustainer_lower), MIN(95, sustainer_upper));
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
				/* if charger limit has been set don't control sustainer again */
				if (charger_sustainer_percentage() == 100) {
					charger_sustainer_reset();
				}
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

/*****************************************************************************/
/* Host command */

static enum ec_status cmd_charging_limit_control(struct host_cmd_handler_args *args)
{

	const struct ec_params_ec_chg_limit_control *p = args->params;
	struct ec_response_chg_limit_control *r = args->response;

	if (p->modes & CHG_LIMIT_DISABLE) {
		charging_maximum_level = 0;
		system_set_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, 0);
	}

	if (p->modes & CHG_LIMIT_SET_LIMIT) {
		if (p->max_percentage < 20)
			return EC_RES_ERROR;

		charging_maximum_level = p->max_percentage;
		system_set_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, charging_maximum_level);
	}

	if (p->modes & CHG_LIMIT_OVERRIDE)
		charging_maximum_level = charging_maximum_level | CHG_LIMIT_OVERRIDE;

	if (p->modes & CHG_LIMIT_GET_LIMIT) {
		system_get_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, &charging_maximum_level);
		r->max_percentage = charging_maximum_level;
		args->response_size = sizeof(*r);
	}

	battery_percentage_control();

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_LIMIT_CONTROL, cmd_charging_limit_control,
			EC_VER_MASK(0));

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
				charger_sustainer_reset();
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
		CPRINTF("\tsustainer percentage:\n");
		if (stage == BATT_EXTENDER_STAGE_1) {
			sustainer_lower = MIN(90, sustainer_lower);
			sustainer_upper = MIN(95, sustainer_upper);
		} else if (stage == BATT_EXTENDER_STAGE_2) {
			sustainer_lower = MIN(85, sustainer_lower);
			sustainer_upper = MIN(87, sustainer_upper);
		}

		CPRINTF("\tlower: %d, upper: %d\n", sustainer_lower, sustainer_upper);
		CPRINTF("\tUser charge limit:%d\n", charging_maximum_level);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battextender, cmd_batt_extender,
			"[enable/disable/days/reset/manual][days:1-99][reset:1-9999][manual:1/0]",
			"battery extender control");
