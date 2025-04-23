/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "amd_stt.h"
#include "board_host_command.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "system.h"
#include "thermal.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"
#include "temperature_filter.h"

#include "temp_sensor/f75303.h"
#include "temp_sensor/f75397.h"

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

/* The macro of temperature IDs */
#define TEMP_ID_POWER	TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_power))
#define TEMP_ID_DDR	TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_memory))
#define TEMP_ID_AMBIENT	TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_ambient))
#define TEMP_ID_CHIPSET	TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_chipset))

/* The macro of temperature sensor F75303 IDs */
#define F75303_ID_UTH1	F75303_SENSOR_ID(DT_NODELABEL(power_f75303))
#define F75303_ID_QTH1	F75303_SENSOR_ID(DT_NODELABEL(memory_f75303))
#define F75303_ID_QTH2	F75397_SENSOR_ID(DT_NODELABEL(ambient_f75303))

/* The macro of fan (same as the fan channel) */
#define FAN_APU		DT_NODELABEL(fan_apu)
#define FAN_CHASSIS_1	DT_NODELABEL(fan_front)
#define FAN_CHASSIS_2	DT_NODELABEL(fan_rsv)

/* Follow BIOS EC and SW ERS to define the fan off temperature (unit: C) */
#define FAN_OFF_MIN_TEMP_HYSTERESIS 1

struct fan_parameter_t fan_params[FAN_CH_COUNT];

void fan_init(void)
{
	/**
	 * Before the SW setting the fans parameterm, EC should have a
	 * default value to ensure the system can be power on.
	 *
	 * TODO: Need thermal team and customer to define the initial value
	 */
	for (int idx = 0; idx < FAN_CH_COUNT; idx++) {
		fan_params[idx].max_duty = 100;
		fan_params[idx].min_duty = 20;
		fan_params[idx].max_temperature = 54;
		fan_params[idx].min_temperature = 40;
		fan_params[idx].fan_always_on = false;
		if (idx == FAN_APU)
			fan_params[idx].sensor_source = SENSOR_SRC_APU;
		else
			fan_params[idx].sensor_source = SENSOR_SRC_OFF;
		fan_params[idx].target_duty = 0;
	}
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_DEFAULT);

/* Get temperature (uint: mk) */
int board_get_power_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return f75303_get_val_mk(F75303_ID_UTH1, temp_mk);
}

int board_get_memory_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return f75303_get_val_mk(F75303_ID_QTH1, temp_mk);
}

int board_get_ambient_temp_mk(int *temp_mk)
{
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	return f75303_get_val_mk(F75303_ID_QTH2, temp_mk);
}

static int thermal_fan_percent_with_hysteresis(int low, int high, int cur, bool fan_is_on)
{
	int hysteresis_low = (low - FAN_OFF_MIN_TEMP_HYSTERESIS);

	/* Set hysteresis target point to minimum temperature - FAN_OFF_MIN_TEMP_HYSTERESIS */
	if (fan_is_on && cur < hysteresis_low)
		return 0;
	else if (fan_is_on && cur >= hysteresis_low && cur <= low) {
		/**
		 * If the sensor temperature in hysteresis range, return duty to 1,
		 * EC will set the duty to fan_param->min_duty.
		 */
		return 1;
	} else if (!fan_is_on && cur < low)
		return 0;

	if (cur > high)
		return 100;
	return 100 * (cur - low) / (high - low);
}

static int thermal_process_sensor_source_apu(int fan, int *temp)
{
	int duty;
	struct fan_parameter_t *fan_param = &fan_params[fan];

	duty = thermal_fan_percent_with_hysteresis(fan_param->min_temperature,
				   fan_param->max_temperature,
				   temp[TEMP_ID_POWER],
				   fan_param->target_duty ? true : false);

	if (duty && duty < fan_param->min_duty)
		duty = fan_param->min_duty;

	if (duty && duty > fan_param->max_duty)
		duty = fan_param->max_duty;

	return duty;
}

static int thermal_process_sensor_source_chassis(int fan, int *temp)
{
	int duty;
	struct fan_parameter_t *fan_param = &fan_params[fan];

	duty = thermal_fan_percent_with_hysteresis(fan_param->min_temperature,
				   fan_param->max_temperature,
				   temp[TEMP_ID_AMBIENT],
				   fan_param->target_duty ? true : false);

	if (duty && duty < fan_param->min_duty)
		duty = fan_param->min_duty;

	if (duty && duty > fan_param->max_duty)
		duty = fan_param->max_duty;

	return duty;
}

static void thermal_set_fan_duty(int fan, int duty)
{
	fan_set_rpm_mode(fan, 0);
	fan_set_duty(fan, duty);
}

/* Main function */
void board_override_fan_control(int fan, int *temp)
{
	struct fan_parameter_t *fan_param = &fan_params[fan];

	if (!is_thermal_control_enabled(fan))
		return;

	/* TODO: should we use the unit mk to calculate the target duty? */
	if (fan_param->sensor_source == SENSOR_SRC_APU)
		fan_param->target_duty = thermal_process_sensor_source_apu(fan, temp);
	else if (fan_param->sensor_source == SENSOR_SRC_CHASSIS)
		fan_param->target_duty = thermal_process_sensor_source_chassis(fan, temp);
	else
		fan_param->target_duty = 0;

	if (fan_param->target_duty == 0)
		fan_param->target_duty = fan_param->fan_always_on ? fan_param->min_duty : 0;

	thermal_set_fan_duty(fan, fan_param->target_duty);
}

/* Console command */
static int cmd_fan_control(int argc, const char **argv)
{
	char *e;
	int fan_index;
	struct fan_parameter_t *fan_param;

	if (argc >= 3) {
		fan_index = strtoi(argv[2], &e, 0);
		if (*e || fan_index >= FAN_CH_COUNT)
			return EC_ERROR_PARAM2;

		fan_param = &fan_params[fan_index];
	}

	/* Get the parameters */
	if (argc == 3) {
		static const char * const source[] = { "NULL", "APU", "CHASSIS",};

		if (strcasecmp(argv[1], "get") == 0) {
			CPRINTS("Fan%d parameters:", fan_index);
			CPRINTS("    Target Duty:%d", fan_param->target_duty);
			CPRINTS("    Maximum Duty:%d", fan_param->max_duty);
			CPRINTS("    Minimum Duty:%d", fan_param->min_duty);
			CPRINTS("    Maximum Temperature:%d", fan_param->max_temperature);
			CPRINTS("    Minimum Temperature:%d", fan_param->min_temperature);
			CPRINTS("    Sensor Source:%s", source[fan_param->sensor_source]);
			CPRINTS("    Always on %sabled", fan_param->fan_always_on ? "en" : "dis");
			return EC_SUCCESS;
		} else
			return EC_ERROR_PARAM1;
	}

	/* Set the parameters */
	if (argc > 3) {
		if (strcasecmp(argv[1], "set") == 0) {
			int value, param;

			for (param = 3; param < argc; param++) {
				value = strtoi(argv[param], &e, 0);
				if (*e)
					return EC_ERROR_PARAM1 + param - 1;
				if (value < 0)
					continue;
				switch (param) {
				case 3:
					fan_param->max_duty = (value > 100) ? 100 : value;
					break;
				case 4:
					fan_param->min_duty = (value > 100) ? 100 : value;
					break;
				case 5:
					fan_param->max_temperature = value;
					break;
				case 6:
					fan_param->min_temperature = value;
					break;
				case 7:
					/* APU FAN sensor source can't be changed */
					if (fan_index == FAN_APU ||
					    (value != SENSOR_SRC_APU &&
					     value != SENSOR_SRC_CHASSIS))
						break;

					fan_param->sensor_source = value;
					break;
				case 8:
					fan_param->fan_always_on = (value > 0) ? true : false;
					break;
				}
			}

			return EC_SUCCESS;
		} else
			return EC_ERROR_PARAM1;
	}

	return EC_ERROR_PARAM_COUNT;
}
DECLARE_CONSOLE_COMMAND(fanctl, cmd_fan_control,
			"get/set fanidx"
			" [max duty[min duty[max temp[min temp[source[always on]]]]]]",
			"Set fan parameters (degrees Celsius). Use -1 to skip.");

/* Host command */
static enum ec_status hc_fan_configuration(struct host_cmd_handler_args *args)
{
	const struct ec_params_fan_configuration *p = args->params;
	struct ec_response_fan_configuration *r = args->response;
	struct fan_parameter_t *fan;

	if (p->fan_index >= FAN_CH_COUNT)
		return EC_ERROR_PARAM1;

	fan = &fan_params[p->fan_index];

	if (p->command == FAN_HC_CMD_SET) {
		memcpy(fan, &p->fan_config, sizeof(p->fan_config));
		/* avoid the sensor source of the APU fan to be changed */
		if (p->fan_index == FAN_APU)
			fan->sensor_source = SENSOR_SRC_APU;
	}

	memcpy(&r->fan_config, fan, sizeof(*r));
	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FAN_CONFIGURATION, hc_fan_configuration, EC_VER_MASK(0));
