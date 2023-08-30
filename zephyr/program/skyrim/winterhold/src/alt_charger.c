/* Copyright 2022 The ChromiumOS Authors
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
#include "ztest/alt_charger.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(winterhold, CONFIG_SKYRIM_LOG_LEVEL);

static void alt_charger_init(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_CHARGER, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FW_CHARGER);
		return;
	}

	if (val == FW_CHARGER_ISL9538)
		CHG_ENABLE_ALTERNATE(0);
}
DECLARE_HOOK(HOOK_INIT, alt_charger_init, HOOK_PRIO_POST_FIRST);

static void charger_set_frequence_to_1000khz(void)
{
	charger_set_frequency(1000);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, charger_set_frequence_to_1000khz,
	     HOOK_PRIO_DEFAULT);

static void charger_set_frequence_to_635khz(void)
{
	if (extpower_is_present() && charge_get_percent() == 100)
		charger_set_frequency(635);
	else
		charger_set_frequency(1000);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, charger_set_frequence_to_635khz,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, charger_set_frequence_to_635khz,
	     HOOK_PRIO_DEFAULT);
