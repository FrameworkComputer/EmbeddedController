/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include "common.h"
#include "accelgyro.h"
#include "cros_board_info.h"
#include "hooks.h"
#include "motionsense_sensors.h"

LOG_MODULE_DECLARE(skyrim, CONFIG_SKYRIM_LOG_LEVEL);

/*
 * Mainboard orientation support.
 */

#define ALT_MAT SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(lid_rot_ref1))
#define LID_ACCEL SENSOR_ID(DT_NODELABEL(lid_accel))

static void form_factor_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * If the board version >=4
	 * use ver1 rotation matrix.
	 */
	ret = cbi_get_board_version(&val);
	if (ret == EC_SUCCESS && val >= 4) {
		LOG_INF("Switching to ver1 lid");
		motion_sensors[LID_ACCEL].rot_standard_ref = &ALT_MAT;
	}
}
DECLARE_HOOK(HOOK_INIT, form_factor_init, HOOK_PRIO_POST_I2C);
