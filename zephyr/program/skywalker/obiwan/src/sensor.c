/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "cros_board_info.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12_public.h"
#include "hooks.h"
#include "motionsense_sensors.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_sensor, LOG_LEVEL_INF);

static bool lid_uses_bma422;

void lid_accel_interrupt(enum gpio_signal signal)
{
	if (lid_uses_bma422) {
		bma4xx_interrupt(signal);
	} else {
		lis2dw12_interrupt(signal);
	}
}

static void alt_sensor_init(void)
{
	int ret;
	uint32_t board_version;

	ret = cbi_get_board_version(&board_version);
	if (ret != EC_SUCCESS) {
		lid_uses_bma422 = false;
		LOG_ERR("Error retrieving CBI board version");
		return;
	}

	if (board_version >= 1) {
		lid_uses_bma422 = true;
		LOG_INF("Replace lis2dw12 with bma422");
		MOTIONSENSE_ENABLE_ALTERNATE(alt_lid_accel);
	}
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);
