/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(crystaldrift, CONFIG_SKYRIM_LOG_LEVEL);

test_export_static void clamshell_init(void)
{
	int ret;
	uint32_t val;

	/* Check the form factor from CBI */
	ret = cros_cbi_get_fw_config(FW_FORM_FACTOR, &val);
	if (ret != 0) {
		LOG_ERR("Cannot get FW_FORM_FACTOR");
		return;
	}

	if (val == FW_FF_CLAMSHELL) {
		motion_sensor_count = 0;
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_accel_gyro));
		gmr_tablet_switch_disable();
	}
}
DECLARE_HOOK(HOOK_INIT, clamshell_init, HOOK_PRIO_POST_DEFAULT);
