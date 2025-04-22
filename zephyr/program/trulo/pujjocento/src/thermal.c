/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "body_detection.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

/*AMB sensor for thermal table control*/
#define TEMP_AMB TEMP_SENSOR_ID(DT_NODELABEL(memory_thermistor))
/*SOC and CPU sensor for fan table control*/
#define TEMP_SOC TEMP_SENSOR_ID(DT_NODELABEL(charger_thermistor))
#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(ambient_thermistor))

static const struct ec_thermal_config thermal_desktop_lid_open = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(43),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(97),
		[EC_TEMP_THRESH_HALT] = C_TO_K(98),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(39),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(87),
		[EC_TEMP_THRESH_HALT] = C_TO_K(88),
	},
};

static const struct ec_thermal_config thermal_desktop_lid_close = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(43),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(97),
		[EC_TEMP_THRESH_HALT] = C_TO_K(98),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(39),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(87),
		[EC_TEMP_THRESH_HALT] = C_TO_K(88),
	},
};

static const struct ec_thermal_config thermal_laptop = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(42),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(97),
		[EC_TEMP_THRESH_HALT] = C_TO_K(98),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(38),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(87),
		[EC_TEMP_THRESH_HALT] = C_TO_K(88),
	},
};

static const struct ec_thermal_config fan_soc_desktop_lid_open = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(97),
		[EC_TEMP_THRESH_HALT] = C_TO_K(98),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(87),
		[EC_TEMP_THRESH_HALT] = C_TO_K(88),
	},
};

static const struct ec_thermal_config fan_soc_desktop_lid_close = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(97),
		[EC_TEMP_THRESH_HALT] = C_TO_K(98),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(87),
		[EC_TEMP_THRESH_HALT] = C_TO_K(88),
	},
};

static const struct ec_thermal_config fan_soc_laptop = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(97),
		[EC_TEMP_THRESH_HALT] = C_TO_K(98),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(87),
		[EC_TEMP_THRESH_HALT] = C_TO_K(88),
	},
};

/* Switch thermal table when mode change */
static void thermal_table_switch(void)
{
	enum body_detect_states body_state = body_detect_get_state();

	if (body_state == BODY_DETECTION_OFF_BODY) {
		if (lid_is_open()) {
			thermal_params[TEMP_AMB] = thermal_desktop_lid_open;
			thermal_params[TEMP_SOC] = fan_soc_desktop_lid_open;
			CPRINTS("Thermal: Desktop lid open mode");
		} else {
			thermal_params[TEMP_AMB] = thermal_desktop_lid_close;
			thermal_params[TEMP_SOC] = fan_soc_desktop_lid_close;
			CPRINTS("Thermal: Desktop lid close mode");
		}
	} else {
		thermal_params[TEMP_AMB] = thermal_laptop;
		thermal_params[TEMP_SOC] = fan_soc_laptop;
		CPRINTS("Thermal: Laptop mode");
	}
}
DECLARE_HOOK(HOOK_INIT, thermal_table_switch, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_LID_CHANGE, thermal_table_switch, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BODY_DETECT_CHANGE, thermal_table_switch, HOOK_PRIO_DEFAULT);
