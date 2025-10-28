/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "driver/charger/bq257x0_regs.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_pd.h"

#include <zephyr/logging/log.h>

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	if (battery_is_present() == BP_YES) {
		/*
		 * Limit current to 98% when AC+DC, for
		 * CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT is set to 2.
		 */
		charge_set_input_current_limit(charge_ma, charge_mv);
	} else {
		/* Limit current to 100% when AC only */
		charger_set_input_current_limit(0, charge_ma);
	}
}
