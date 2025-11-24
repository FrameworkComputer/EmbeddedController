/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12_public.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uldrenite_sensor, LOG_LEVEL_INF);

static int sensor_fwconfig;
static int base_use_alt_sensor;
static int lid_use_alt_sensor;

enum base_sensor_type {
	base_lis2dw12 = 0,
	base_bma422,
};

enum lid_sensor_type {
	lid_lis2dw12 = 0,
	lid_bma422,
};

void motion_interrupt(enum gpio_signal signal)
{
	if (lid_use_alt_sensor == lid_lis2dw12)
		lis2dw12_interrupt(signal);
	else
		bma4xx_interrupt(signal);
}

void lid_accel_interrupt(enum gpio_signal signal)
{
	if (base_use_alt_sensor == base_lis2dw12)
		lis2dw12_interrupt(signal);
	else
		bma4xx_interrupt(signal);
}

static void motionsense_init(void)
{
	int ret;
	ret = cros_cbi_get_fw_config(FORM_FACTOR, &sensor_fwconfig);
	if (ret < 0) {
		LOG_ERR("error retriving CBI config: %d", ret);
		return;
	}

	if (sensor_fwconfig == FORM_FACTOR_CLAMSHELL) {
		if (IS_ENABLED(CONFIG_GMR_TABLET_MODE)) {
			/* Disable GMR interrupt on EC that have GMR code
			 * support compiled-in. */
			gmr_tablet_switch_disable();
		}
		motion_sensor_count = 0;
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_acc_int_l),
				      GPIO_INPUT | GPIO_PULL_UP);
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_imu_int_l),
				      GPIO_INPUT | GPIO_PULL_UP);
		LOG_INF("Board is Clamshell");
	} else if (sensor_fwconfig == FORM_FACTOR_CONVERTIBLE) {
		LOG_INF("Board is Convertible");
	}
}
DECLARE_HOOK(HOOK_INIT, motionsense_init, HOOK_PRIO_DEFAULT);

test_export_static void alt_sensor_init(void)
{
	/* Check which motion sensors are used */
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_0)))) {
		LOG_INF("Base : LIS2DWL");
		base_use_alt_sensor = base_lis2dw12;
	} else {
		LOG_INF("Base : BMA422");
		base_use_alt_sensor = base_bma422;
	}
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(lid_sensor_0)))) {
		LOG_INF("Lid : LIS2DWL");
		lid_use_alt_sensor = lid_lis2dw12;
	} else {
		LOG_INF("Lid : BMA422");
		lid_use_alt_sensor = lid_bma422;
	}
	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);
