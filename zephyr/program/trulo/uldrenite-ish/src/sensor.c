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
#include "driver/accelgyro_lsm6dsm.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/logging/log.h>

#define I2C_PORT_SENSOR 1

LOG_MODULE_REGISTER(uldrenite_sensor, LOG_LEVEL_INF);

enum base_sensor_type {
	base_lis2dw12 = 0,
	base_lsm6ds3tr,
};

static int sensor_fwconfig;
static int base_use_alt_sensor;

void motion_interrupt(enum gpio_signal signal)
{
	if (base_use_alt_sensor == base_lis2dw12)
		lis2dw12_interrupt(signal);
	else if (base_use_alt_sensor == base_lsm6ds3tr)
		lsm6dsm_interrupt(signal);
}

void lid_accel_interrupt(enum gpio_signal signal)
{
	if (sensor_fwconfig == FORM_FACTOR_CONVERTIBLE)
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
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_acc_int_l),
				      GPIO_INPUT | GPIO_PULL_UP);
		LOG_INF("Board is Clamshell");
		if (IS_ENABLED(
			    CONFIG_PLATFORM_EC_BODY_DETECTION_DYNAMIC_INDEX)) {
			motion_sense_set_on_body_sensor_index(
				SENSOR_ID(DT_NODELABEL(lid_accel)));
			motion_sensor_count = 1;
		}
	} else if (sensor_fwconfig == FORM_FACTOR_CONVERTIBLE) {
		/* Convertibles use the bma4xx alternate as the lid sensor */
		ENABLE_ALT_MOTION_SENSOR(DT_NODELABEL(alt_lid_accel));
		if (IS_ENABLED(
			    CONFIG_PLATFORM_EC_BODY_DETECTION_DYNAMIC_INDEX)) {
			motion_sense_set_on_body_sensor_index(
				SENSOR_ID(DT_NODELABEL(base_accel)));
		}
		LOG_INF("Board is Convertible");
	}
}
DECLARE_HOOK(HOOK_INIT, motionsense_init, HOOK_PRIO_DEFAULT);

static void alt_sensor_init(void)
{
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_0)))) {
		base_use_alt_sensor = base_lis2dw12;
		LOG_INF("BASE ACCEL IS lis2dw12");
	} else if (cros_cbi_ssfc_check_match(
			   CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_1)))) {
		base_use_alt_sensor = base_lsm6ds3tr;
		LOG_INF("BASE ACCEL IS lsm6ds3tr");
	}

	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);
