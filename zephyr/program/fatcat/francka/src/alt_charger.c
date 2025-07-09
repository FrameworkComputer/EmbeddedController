/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "charger_chips.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/charger/isl923x.h"
#include "extpower.h"
#include "hooks.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#define CPRINTS(format, args...) cprints(CC_LPC, format, ##args)

static void alt_charger_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_CHARGER_CHIP, &val);
	if (ret != 0) {
		CPRINTS("Error retrieving CBI FW_CONFIG field %d",
			FW_CHARGER_CHIP);
		return;
	}

	if (val == FW_CHARGER_ISL9538C)
		CHG_ENABLE_ALTERNATE(0);
}
DECLARE_HOOK(HOOK_INIT, alt_charger_init, HOOK_PRIO_POST_FIRST);
