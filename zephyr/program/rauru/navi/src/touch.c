/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(rauru, CONFIG_RAURU_LOG_LEVEL);

test_export_static void als_cal_init(void)
{
	int ret;
	uint32_t val;

	ret = cbi_get_board_version(&val);
	if (ret != EC_SUCCESS) {
		LOG_ERR("Error retrieving CBI BOARD_VER.");
		return;
	}

	/*
	 * If the firmware config indicates
	 * an craaskbowl form factor, use the alternative
	 * rotation matrix.
	 */
	ret = cros_cbi_get_fw_config(FW_TOUCH, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_TOUCH);
		return;
	}
	if (val == FW_TOUCH_NOT_PRESENT) {
		LOG_INF("Non-touch support");
		MOTIONSENSE_ENABLE_ALTERNATE(alt_als_clear);
		MOTIONSENSE_ENABLE_ALTERNATE(alt_als_rgb);
	} else
		LOG_INF("Touch support");
}
DECLARE_HOOK(HOOK_INIT, als_cal_init, HOOK_PRIO_POST_I2C + 2);
