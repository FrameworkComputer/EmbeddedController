/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "motionsense_sensors.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

#define ALT_MAT SENSOR_ROT_STD_REF_NAME(DT_NODELABEL(lid_rot_inverted))
#define LID_ACCEL SENSOR_ID(DT_NODELABEL(lid_accel))

test_export_static void form_factor_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * If the firmware config indicates
	 * an inverted form factor, use the alternative
	 * rotation matrix.
	 */
	ret = cros_cbi_get_fw_config(FW_LID_INVERSION, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d",
			FW_LID_INVERSION);
		return;
	}
	if (val == SENSOR_INVERTED) {
		LOG_INF("Switching to inverted lid");
		motion_sensors[LID_ACCEL].rot_standard_ref = &ALT_MAT;
	}
}
DECLARE_HOOK(HOOK_INIT, form_factor_init, HOOK_PRIO_POST_I2C);
