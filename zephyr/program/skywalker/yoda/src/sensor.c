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
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(yoda_sensor, LOG_LEVEL_INF);

enum lid_sensor_type {
	LID_LIS2DW12 = 0,
	LID_BMA422,
};

static int lid_use_alt_sensor;
static int sensor_fwconfig;

void lid_accel_interrupt(enum gpio_signal signal)
{
	if (lid_use_alt_sensor == LID_LIS2DW12)
		lis2dw12_interrupt(signal);
	else
		bma4xx_interrupt(signal);
}

test_export_static void alt_sensor_init(void)
{
	/* Check which motion sensors are used */
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(lid_sensor_0)))) {
		LOG_INF("Lid : LIS2DWL");
		lid_use_alt_sensor = LID_LIS2DW12;
	} else {
		LOG_INF("Lid : BMA422");
		lid_use_alt_sensor = LID_BMA422;
	}
	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);

static void tablet_mode_init(void)
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
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_lid_accel));
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_tablet_mode));
		gpio_pin_configure_dt(
			GPIO_DT_FROM_NODELABEL(gpio_lid_accel_int_ec_l),
			GPIO_INPUT | GPIO_PULL_UP);
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_imu_int_ec_l),
				      GPIO_INPUT | GPIO_PULL_UP);
		LOG_INF("Board is Clamshell");
	} else if (sensor_fwconfig == FORM_FACTOR_CONVERTIBLE) {
		LOG_INF("Board is Convertible");
	}
}
DECLARE_HOOK(HOOK_INIT, tablet_mode_init, HOOK_PRIO_INIT_LID);
