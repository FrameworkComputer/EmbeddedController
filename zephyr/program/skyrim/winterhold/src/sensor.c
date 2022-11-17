/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_board_info.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi3xx.h"
#include "hooks.h"
#include "motionsense_sensors.h"

void base_accel_interrupt(enum gpio_signal signal)
{
	int ret;
	uint32_t val;

	ret = cbi_get_board_version(&val);

	if (ret == EC_SUCCESS && val < 1)
		bmi3xx_interrupt(signal);
	else
		lis2dw12_interrupt(signal);
}

static void motionsense_init(void)
{
	int ret;
	uint32_t val;

	ret = cbi_get_board_version(&val);

	if (ret == EC_SUCCESS && val < 1) {
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_accel);
	}
}
DECLARE_HOOK(HOOK_INIT, motionsense_init, HOOK_PRIO_DEFAULT);
