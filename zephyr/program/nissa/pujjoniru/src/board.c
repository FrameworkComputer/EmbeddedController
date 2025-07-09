/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "battery.h"
#include "charge_state.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12_public.h"
#include "ec_commands.h"
#include "fan.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_backlight.h"
#include "keyboard_customization.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "temp_sensor/temp_sensor.h"
#include "thermal.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_INF);
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/* thermal table control for 15W CPU */
#define TEMP_5V TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_5v))
#define TEMP_CPU TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_cpu))
#define TEMP_AMBIENT TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_ambient))
#define TEMP_CHARGER TEMP_SENSOR_ID(DT_NODELABEL(temp_sensor_charger))

const static struct ec_thermal_config thermal_5v = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(88),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(83),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(50),
	.temp_fan_max = C_TO_K(65),
};

const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(87),
		[EC_TEMP_THRESH_HALT] = C_TO_K(90),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(40),
	.temp_fan_max = C_TO_K(51),
};

const static struct ec_thermal_config thermal_ambient = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(72),
		[EC_TEMP_THRESH_HALT] = C_TO_K(75),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(40),
	.temp_fan_max = C_TO_K(50),
};

const static struct ec_thermal_config thermal_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(87),
		[EC_TEMP_THRESH_HALT] = C_TO_K(90),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(55),
	.temp_fan_max = C_TO_K(75),
};

test_export_static void thermal_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * Retrieve the thermal config.
	 */
	ret = cros_cbi_get_fw_config(FW_THERMAL, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_THERMAL);
		return;
	}
	if (val != FW_THERMAL_6W) {
		thermal_params[TEMP_5V] = thermal_5v;
		thermal_params[TEMP_CPU] = thermal_cpu;
		thermal_params[TEMP_AMBIENT] = thermal_ambient;
		thermal_params[TEMP_CHARGER] = thermal_charger;
		CPRINTS("Thermal: 15W thermal control");
	}
}
DECLARE_HOOK(HOOK_INIT, thermal_init, HOOK_PRIO_POST_FIRST);

static bool lid_uses_lis2dw12;

void motion_interrupt(enum gpio_signal signal)
{
	if (lid_uses_lis2dw12) {
		lis2dw12_interrupt(signal);
	} else {
		bma4xx_interrupt(signal);
	}
}

static void alt_sensor_init(void)
{
	lid_uses_lis2dw12 = cros_cbi_ssfc_check_match(
		CBI_SSFC_VALUE_ID(DT_NODELABEL(lid_sensor_lis2dw12)));

	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);
