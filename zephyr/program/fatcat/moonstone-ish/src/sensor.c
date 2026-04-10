/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "motionsense_sensors.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(moonstone_sensor, LOG_LEVEL_INF);

static void sensor_init(void)
{
	motion_sensors_check_ufsc();
}
DECLARE_HOOK(HOOK_INIT, sensor_init, HOOK_PRIO_POST_I2C);
