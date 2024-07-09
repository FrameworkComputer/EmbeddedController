/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi3xx.h"
#include "hooks.h"
#include "motionsense_sensors.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_sensor, LOG_LEVEL_INF);

static int cbi_boardversion = -1;
static int cbi_fwconfig;

void base_accel_interrupt(enum gpio_signal signal)
{
	if (cbi_boardversion == 0)
		bmi3xx_interrupt(signal);
	else if (cbi_boardversion == 1)
		lis2dw12_interrupt(signal);
	else if (cbi_boardversion >= 2) {
		if (cbi_fwconfig == FW_BASE_BMI323)
			bmi3xx_interrupt(signal);
		else if (cbi_fwconfig == FW_BASE_LIS2DW12)
			lis2dw12_interrupt(signal);
	} else
		lis2dw12_interrupt(signal);
}

static void motionsense_init(void)
{
	int ret;

	ret = cbi_get_board_version(&cbi_boardversion);
	if (ret < 0) {
		LOG_ERR("error retriving CBI board revision: %d", ret);
		return;
	}

	ret = cros_cbi_get_fw_config(FW_BASE_SENSOR, &cbi_fwconfig);
	if (ret < 0) {
		LOG_ERR("error retriving CBI config: %d", ret);
		return;
	}

	if (ret == EC_SUCCESS && cbi_boardversion < 1) {
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_accel);
	} else if (cbi_boardversion >= 2) {
		if (cbi_fwconfig == FW_BASE_BMI323) {
			MOTIONSENSE_ENABLE_ALTERNATE(alt_base_accel);
			ccprints("BASE ACCEL is BMI323");
		} else if (cbi_fwconfig == FW_BASE_LIS2DW12) {
			ccprints("BASE ACCEL IS LIS2DW12");
		}
	}
}
DECLARE_HOOK(HOOK_INIT, motionsense_init, HOOK_PRIO_DEFAULT);
