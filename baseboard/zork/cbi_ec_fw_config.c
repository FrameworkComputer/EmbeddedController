/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cbi_ec_fw_config.h"
#include "cros_board_info.h"

/****************************************************************************
 * CBI Zork EC FW Configuration
 */
uint32_t get_cbi_fw_config(void)
{
	static uint32_t cached_fw_config = UNINITIALIZED_FW_CONFIG;

	if (cached_fw_config == UNINITIALIZED_FW_CONFIG) {
		uint32_t val;

		if (cbi_get_fw_config(&val) == EC_SUCCESS)
			cached_fw_config = val;
	}
	return cached_fw_config;
}

/*
 * get_cbi_ec_cfg_usb_db() will return the DB option number.
 */
enum ec_cfg_usb_db_type ec_config_get_usb_db(void)
{
	return ((get_cbi_fw_config() & EC_CFG_USB_DB_MASK)
			>> EC_CFG_USB_DB_L);
}

/*
 * get_cbi_ec_cfg_usb_mb() will return the MB option number.
 */
enum ec_cfg_usb_mb_type ec_config_get_usb_mb(void)
{
	return ((get_cbi_fw_config() & EC_CFG_USB_MB_MASK)
			>> EC_CFG_USB_MB_L);
}

/*
 * ec_config_has_lid_accel_sensor() will return ec_cfg_lid_accel_sensor_type
 */
enum ec_cfg_lid_accel_sensor_type ec_config_has_lid_accel_sensor(void)
{
	return ((get_cbi_fw_config() & EC_CFG_LID_ACCEL_SENSOR_MASK)
			>> EC_CFG_LID_ACCEL_SENSOR_L);
}

/*
 * ec_config_has_base_gyro_sensor() will return ec_cfg_base_gyro_sensor_type
 */
enum ec_cfg_base_gyro_sensor_type ec_config_has_base_gyro_sensor(void)
{
	return ((get_cbi_fw_config() & EC_CFG_BASE_GYRO_SENSOR_MASK)
			>> EC_CFG_BASE_GYRO_SENSOR_L);
}

/*
 * ec_config_has_pwm_keyboard_backlight() will return 1 is present or 0
 */
enum ec_cfg_pwm_keyboard_backlight_type ec_config_has_pwm_keyboard_backlight(
									void)
{
	return ((get_cbi_fw_config() & EC_CFG_PWM_KEYBOARD_BACKLIGHT_MASK)
			>> EC_CFG_PWM_KEYBOARD_BACKLIGHT_L);
}

/*
 * ec_config_has_lid_angle_tablet_mode() will return 1 is present or 0
 */
enum ec_cfg_lid_angle_tablet_mode_type ec_config_has_lid_angle_tablet_mode(
									void)
{
	return ((get_cbi_fw_config() & EC_CFG_LID_ANGLE_TABLET_MODE_MASK)
			>> EC_CFG_LID_ANGLE_TABLET_MODE_L);
}
