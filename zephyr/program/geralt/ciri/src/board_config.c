/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "common.h"
#include "cros_cbi.h"
#include "driver/accelgyro_bmi3xx.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_ERR);

static bool lid_use_alt_sensor;

void motion_interrupt(enum gpio_signal signal)
{
	if (lid_use_alt_sensor) {
		bmi3xx_interrupt(signal);
	} else {
		lsm6dsm_interrupt(signal);
	}
}

static void alt_sensor_init(void)
{
	lid_use_alt_sensor = cros_cbi_ssfc_check_match(
		CBI_SSFC_VALUE_ID(DT_NODELABEL(lid_sensor_0)));

	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);
