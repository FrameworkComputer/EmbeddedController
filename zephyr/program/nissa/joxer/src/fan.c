/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "fan.h"
#include "gpio/gpio.h"
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/*
 * Joxer fan support
 */
test_export_static void fan_init(void)
{
	int ret;
	uint32_t val;
	/*
	 * Retrieve the fan config.
	 */
	ret = cros_cbi_get_fw_config(FW_FAN, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_FAN);
		return;
	}
	if (val != FW_FAN_PRESENT) {
		/* Disable the fan */
		fan_set_count(0);
	} else {
		/* Configure the fan enable GPIO */
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_fan_enable),
				      GPIO_OUTPUT);
	}

	ret = cbi_get_board_version(&val);
	if (ret != EC_SUCCESS) {
		LOG_ERR("Error retrieving CBI BOARD_VER.");
		return;
	}
	if (val > 1) {
		fan_config[0].tach = DEVICE_DT_GET(DT_NODELABEL(tach0));
	}
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_POST_FIRST);
