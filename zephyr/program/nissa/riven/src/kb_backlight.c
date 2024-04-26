/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_config.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

__override uint32_t board_override_feature_flags0(uint32_t flags0)
{
	int ret;
	uint32_t val;

	/*
	 * Remove keyboard backlight feature for devices that don't support it.
	 */
	ret = cros_cbi_get_fw_config(FW_KB_BL, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_KB_BL);
		return flags0;
	}

	if (val == FW_KB_BL_NOT_PRESENT)
		return (flags0 & ~EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB));
	else
		return flags0;
}
