/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12_public.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_lsm6dso.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/*
 * Mainboard orientation support.
 */

#define LIS_ALT_MAT SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(lid_rot_bma422))
#define BMA_ALT_MAT SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(lid_rot_ref))
#define ALT_MAT SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(base_rot_ver1))
#define LID_SENSOR SENSOR_ID(DT_NODELABEL(lid_accel))
#define BASE_SENSOR SENSOR_ID(DT_NODELABEL(base_accel))
#define BASE_GYRO SENSOR_ID(DT_NODELABEL(base_gyro))
#define ALT_LID_S SENSOR_ID(DT_NODELABEL(alt_lid_accel))

enum base_sensor_type {
	base_lsm6dso = 0,
	base_bmi323,
	base_bma422,
};

enum lid_sensor_type {
	lid_lis2dw12 = 0,
	lid_bma422,
};

static int use_alt_sensor;
static bool use_alt_lid_accel;

void motion_interrupt(enum gpio_signal signal)
{
	if (use_alt_sensor == base_bmi323)
		bmi3xx_interrupt(signal);
	else if (use_alt_sensor == base_bma422)
		bma4xx_interrupt(signal);
	else
		lsm6dso_interrupt(signal);
}

void lid_accel_interrupt(enum gpio_signal signal)
{
	if (use_alt_lid_accel == lid_bma422)
		bma4xx_interrupt(signal);
	else
		lis2dw12_interrupt(signal);
}

test_export_static void form_factor_init(void)
{
	int ret;
	uint32_t val;

	ret = cbi_get_board_version(&val);
	if (ret != EC_SUCCESS) {
		LOG_ERR("Error retrieving CBI BOARD_VER.");
		return;
	}

	/*
	 * If the board version is 1
	 * use ver1 rotation matrix.
	 */
	if (val == 1) {
		LOG_INF("Switching to ver1 base");
		motion_sensors[BASE_SENSOR].rot_standard_ref = &ALT_MAT;
		motion_sensors[BASE_GYRO].rot_standard_ref = &ALT_MAT;
	}

	/*
	 * If the firmware config indicates
	 * an craaskbowl form factor, use the alternative
	 * rotation matrix.
	 */
	ret = cros_cbi_get_fw_config(FW_LID_INVERSION, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d",
			FW_LID_INVERSION);
		return;
	}
	if (val == FW_LID_XY_ROT_180) {
		LOG_INF("Lid sensor placement rotate 180 on xy plane");
		motion_sensors[LID_SENSOR].rot_standard_ref = &LIS_ALT_MAT;
		motion_sensors_alt[ALT_LID_S].rot_standard_ref = &BMA_ALT_MAT;
	}
}
DECLARE_HOOK(HOOK_INIT, form_factor_init, HOOK_PRIO_POST_I2C);

test_export_static void alt_sensor_init(void)
{
	int ret;
	uint32_t val;

	/* Check if it's clamshell or convertible */

	ret = cros_cbi_get_fw_config(FORM_FACTOR, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FORM_FACTOR);
		return;
	}
	if (val == CLAMSHELL)
		return;

	/* check which motion sensors are used */
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_1)))) {
		use_alt_sensor = base_bmi323;
		ccprints("BASE ACCEL IS BMI323");
	} else if (cros_cbi_ssfc_check_match(
			   CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_2)))) {
		use_alt_sensor = base_bma422;
		motion_sensor_count--;
		ccprints("BASE ACCEL IS BMA422");
	} else {
		use_alt_sensor = base_lsm6dso;
		ccprints("BASE ACCEL IS LSM6DSO");
	}

	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(lid_sensor_1)))) {
		use_alt_lid_accel = lid_bma422;
		ccprints("LID SENSOR IS BMA422");
	} else {
		use_alt_lid_accel = lid_lis2dw12;
		ccprints("LID SENSOR IS LIS2DW12");
	}

	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C + 1);

test_export_static void clamshell_init(void)
{
	int ret;
	uint32_t val;

	/* Check if it's clamshell or convertible */
	ret = cros_cbi_get_fw_config(FORM_FACTOR, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FORM_FACTOR);
		return;
	}
	if (val == CLAMSHELL) {
		LOG_INF("Clamshell: disable motionsense function.");
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_imu_int_l),
				      GPIO_INPUT | GPIO_PULL_UP);
	}
}
DECLARE_HOOK(HOOK_INIT, clamshell_init, HOOK_PRIO_POST_DEFAULT);
