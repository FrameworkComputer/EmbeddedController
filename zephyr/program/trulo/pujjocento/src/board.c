/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "fan.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_backlight.h"
#include "keyboard_config.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <drivers/vivaldi_kbd.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_INF);
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

static bool has_backlight = FW_KB_BL_NOT_PRESENT;
int8_t board_vivaldi_keybd_idx(void)
{
	if (has_backlight == FW_KB_BL_NOT_PRESENT) {
		return VIVALDI_CFG_IDX(kbd_config_0);
	} else {
		return VIVALDI_CFG_IDX(kbd_config_1);
	}
}

/*
 * Keyboard function decided by FW config.
 */
test_export_static void kb_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_KB_BL, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_KB_BL);
		return;
	}

	if (val == FW_KB_BL_PRESENT) {
		CPRINTS("Keyboard configuration FW_KB_BL_PRESENT.");
		has_backlight = FW_KB_BL_PRESENT;
	} else {
		CPRINTS("Keyboard configuration FW_KB_BL_NOT_PRESENT.");
		has_backlight = FW_KB_BL_NOT_PRESENT;
	}
}
DECLARE_HOOK(HOOK_INIT, kb_init, HOOK_PRIO_POST_I2C);

/* thermal table control for 15W CPU */
#define TEMP_VR TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_vr))
#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_cpu))
#define TEMP_AMBIENT TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_ambient))
#define TEMP_CHARGER TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_charger))

const static struct ec_thermal_config thermal_vr = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(93),
		[EC_TEMP_THRESH_HALT] = C_TO_K(95),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(88),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(51),
	.temp_fan_max = C_TO_K(67),
};

const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(93),
		[EC_TEMP_THRESH_HALT] = C_TO_K(95),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(88),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(50),
	.temp_fan_max = C_TO_K(70),
};

const static struct ec_thermal_config thermal_ambient = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(77),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(40),
	.temp_fan_max = C_TO_K(50),
};

const static struct ec_thermal_config thermal_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(93),
		[EC_TEMP_THRESH_HALT] = C_TO_K(95),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(55),
	.temp_fan_max = C_TO_K(75),
};

test_export_static void fan_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * Retrieve the fan config.
	 */
	ret = cros_cbi_get_fw_config(FW_FAN, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_FAN);
		return;
	}
	if (val != FW_FAN_PRESENT) {
		/* Disable the fan */
		fan_set_count(0);
	} else {
		thermal_params[TEMP_VR] = thermal_vr;
		thermal_params[TEMP_CPU] = thermal_cpu;
		thermal_params[TEMP_AMBIENT] = thermal_ambient;
		thermal_params[TEMP_CHARGER] = thermal_charger;
		CPRINTS("Thermal: 15W thermal control");
	}
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_POST_FIRST);
