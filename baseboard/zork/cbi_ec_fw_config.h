/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _ZORK_CBI_EC_FW_CONFIG__H_
#define _ZORK_CBI_EC_FW_CONFIG__H_

#include "cbi_ssfc.h"

/****************************************************************************
 * CBI Zork EC FW Configuration
 */
#define UNINITIALIZED_FW_CONFIG 0xFFFFFFFF

/*
 * USB Daughter Board (4 bits)
 *
 * get_cbi_ec_cfg_usb_db() will return the DB option number.
 * The option number will be defined in a variant or board level enumeration
 */
#define EC_CFG_USB_DB_L				0
#define EC_CFG_USB_DB_H				3
#define EC_CFG_USB_DB_MASK \
				GENMASK(EC_CFG_USB_DB_H,\
					EC_CFG_USB_DB_L)

/*
 * USB Main Board (4 bits)
 *
 * get_cbi_ec_cfg_usb_mb() will return the MB option number.
 * The option number will be defined in a variant or board level enumeration
 */
#define EC_CFG_USB_MB_L				4
#define EC_CFG_USB_MB_H				7
#define EC_CFG_USB_MB_MASK \
				GENMASK(EC_CFG_USB_MB_H,\
					EC_CFG_USB_MB_L)

/*
 * Lid Accelerometer Sensor (3 bits)
 *
 * ec_config_has_lid_accel_sensor() will return ec_cfg_lid_accel_sensor_type
 */
enum ec_cfg_lid_accel_sensor_type {
	LID_ACCEL_NONE = 0,
	LID_ACCEL_KX022 = 1,
	LID_ACCEL_LIS2DWL = 2,
};
#define EC_CFG_LID_ACCEL_SENSOR_L		8
#define EC_CFG_LID_ACCEL_SENSOR_H		10
#define EC_CFG_LID_ACCEL_SENSOR_MASK	\
				GENMASK(EC_CFG_LID_ACCEL_SENSOR_H,\
					EC_CFG_LID_ACCEL_SENSOR_L)

/*
 * Base Gyro Sensor (3 bits)
 *
 * ec_config_has_base_gyro_sensor() will return ec_cfg_base_gyro_sensor_type
 */
#define EC_CFG_BASE_GYRO_SENSOR_L		11
#define EC_CFG_BASE_GYRO_SENSOR_H		13
#define EC_CFG_BASE_GYRO_SENSOR_MASK	\
				GENMASK(EC_CFG_BASE_GYRO_SENSOR_H,\
					EC_CFG_BASE_GYRO_SENSOR_L)

/*
 * PWM Keyboard Backlight (1 bit)
 *
 * ec_config_has_pwm_keyboard_backlight() will return 1 is present or 0
 */
enum ec_cfg_pwm_keyboard_backlight_type {
	PWM_KEYBOARD_BACKLIGHT_NO = 0,
	PWM_KEYBOARD_BACKLIGHT_YES = 1,
};
#define EC_CFG_PWM_KEYBOARD_BACKLIGHT_L		14
#define EC_CFG_PWM_KEYBOARD_BACKLIGHT_H		14
#define EC_CFG_PWM_KEYBOARD_BACKLIGHT_MASK \
				GENMASK(EC_CFG_PWM_KEYBOARD_BACKLIGHT_H,\
					EC_CFG_PWM_KEYBOARD_BACKLIGHT_L)

/*
 * Lid Angle Tablet Mode (1 bit)
 *
 * ec_config_has_lid_angle_tablet_mode() will return 1 is present or 0
 */
enum ec_cfg_lid_angle_tablet_mode_type {
	LID_ANGLE_TABLET_MODE_NO = 0,
	LID_ANGLE_TABLET_MODE_YES = 1,
};
#define EC_CFG_LID_ANGLE_TABLET_MODE_L		15
#define EC_CFG_LID_ANGLE_TABLET_MODE_H		15
#define EC_CFG_LID_ANGLE_TABLET_MODE_MASK \
				GENMASK(EC_CFG_LID_ANGLE_TABLET_MODE_H,\
					EC_CFG_LID_ANGLE_TABLET_MODE_L)

/*
 * LTE Modem Present (1 bit)
 *
 * ec_config_lte_present() will return 1 if present else 0.
 */
enum ec_cfg_lte_present_type {
	LTE_NONE = 0,
	LTE_PRESENT = 1,
};
#define EC_CFG_LTE_PRESENT_L		29
#define EC_CFG_LTE_PRESENT_H		29
#define EC_CFG_LTE_PRESENT_MASK \
				GENMASK(EC_CFG_LTE_PRESENT_H,\
					EC_CFG_LTE_PRESENT_L)

/*
 * Keyboard Layout (2 bit)
 *
 * ec_config_keyboard_layout() will return keyboard layout type.
 */
enum ec_cfg_keyboard_layout_type {
	KB_LAYOUT_DEFAULT = 0,
	KB_LAYOUT_1 = 1,
};
#define EC_CFG_KEYBOARD_LAYOUT_L		30
#define EC_CFG_KEYBOARD_LAYOUT_H		31
#define EC_CFG_KEYBOARD_LAYOUT_MASK \
				GENMASK(EC_CFG_KEYBOARD_LAYOUT_H,\
					EC_CFG_KEYBOARD_LAYOUT_L)


uint32_t get_cbi_fw_config(void);
enum ec_cfg_usb_db_type ec_config_get_usb_db(void);
enum ec_cfg_usb_mb_type ec_config_get_usb_mb(void);
enum ec_cfg_lid_accel_sensor_type ec_config_has_lid_accel_sensor(void);
enum ec_ssfc_base_gyro_sensor ec_config_has_base_gyro_sensor(void);
enum ec_cfg_pwm_keyboard_backlight_type ec_config_has_pwm_keyboard_backlight(
									void);
enum ec_cfg_lid_angle_tablet_mode_type ec_config_has_lid_angle_tablet_mode(
									void);
enum ec_cfg_lte_present_type ec_config_lte_present(void);
enum ec_cfg_keyboard_layout_type ec_config_keyboard_layout(void);

#endif /* _ZORK_CBI_EC_FW_CONFIG__H_ */
