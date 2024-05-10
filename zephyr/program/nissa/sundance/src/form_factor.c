/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "button.h"
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

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/*
 * Mainboard orientation support.
 */

#define ALT_MAT SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(base_rot_inverted))
#define BASE_SENSOR SENSOR_ID(DT_NODELABEL(base_accel))
#define BASE_GYRO SENSOR_ID(DT_NODELABEL(base_gyro))

test_export_static void form_factor_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * If the firmware config indicates
	 * an inverted form factor, use the alternative
	 * rotation matrix.
	 */
	ret = cros_cbi_get_fw_config(FW_BASE_INVERSION, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d",
			FW_BASE_INVERSION);
		return;
	}
	if (val == FW_BASE_INVERTED) {
		LOG_INF("Switching to inverted base");
		motion_sensors[BASE_SENSOR].rot_standard_ref = &ALT_MAT;
		motion_sensors[BASE_GYRO].rot_standard_ref = &ALT_MAT;
	}
}
DECLARE_HOOK(HOOK_INIT, form_factor_init, HOOK_PRIO_POST_I2C);
