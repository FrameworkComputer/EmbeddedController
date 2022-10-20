/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "accelgyro.h"
#include "button.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_lsm6dso.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motionsense_sensors.h"
#include "motion_sense.h"
#include "tablet_mode.h"

#include "nissa_common.h"

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

static bool use_alt_sensor;

void motion_interrupt(enum gpio_signal signal)
{
	if (use_alt_sensor)
		bmi3xx_interrupt(signal);
	else
		lsm6dso_interrupt(signal);
}

static void form_factor_init(void)
{
	int ret;
	uint32_t val;
	enum nissa_sub_board_type sb = nissa_get_sb_type();

	ret = cbi_get_board_version(&val);
	if (ret != EC_SUCCESS) {
		LOG_ERR("Error retrieving CBI BOARD_VER.");
		return;
	}
	/*
	 * The volume up/down button are exchanged on ver3 USB
	 * sub board.
	 *
	 * LTE:
	 *   volup -> gpioa2, voldn -> gpio93
	 * USB:
	 *   volup -> gpio93, voldn -> gpioa2
	 */
	if (val == 3 && sb == NISSA_SB_C_A) {
		LOG_INF("Volume up/down btn exchanged on ver3 USB sku");
		buttons[BUTTON_VOLUME_UP].gpio = GPIO_VOLUME_DOWN_L;
		buttons[BUTTON_VOLUME_DOWN].gpio = GPIO_VOLUME_UP_L;
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

	/* check which base sensor is used for motion_interrupt */
	use_alt_sensor = cros_cbi_ssfc_check_match(
		CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_1)));

	motion_sensors_check_ssfc();

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
				      GPIO_DISCONNECTED);
	}
}
DECLARE_HOOK(HOOK_INIT, form_factor_init, HOOK_PRIO_POST_I2C);
