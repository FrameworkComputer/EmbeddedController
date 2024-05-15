/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "common.h"
#include "cros_cbi.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_icm42607.h"
#include "hooks.h"
#include "motionsense_sensors.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_sensor, LOG_LEVEL_INF);

void motion_interrupt(enum gpio_signal signal)
{
	uint32_t val;
	int ret;

	ret = cros_cbi_get_fw_config(FW_BASE_GYRO, &val);
	if (ret < 0) {
		LOG_ERR("error retriving CBI config: %d", ret);
		return;
	}

	if (val == FW_BASE_ICM42607) {
		icm42607_interrupt(signal);
	} else if (val == FW_BASE_BMI323) {
		bmi3xx_interrupt(signal);
	}
}

static void motionsense_init(void)
{
	uint32_t val;
	int ret;

	ret = cros_cbi_get_fw_config(FW_BASE_GYRO, &val);
	if (ret < 0) {
		LOG_ERR("error retriving CBI config: %d", ret);
		return;
	}

	if (val == FW_BASE_ICM42607) {
		ccprints("BASE ACCEL is ICM42607");
	} else if (val == FW_BASE_BMI323) {
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_accel);
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_gyro);
		ccprints("BASE ACCEL IS BMI323");
	} else {
		ccprints("no motionsense");
	}
}
DECLARE_HOOK(HOOK_INIT, motionsense_init, HOOK_PRIO_DEFAULT);
