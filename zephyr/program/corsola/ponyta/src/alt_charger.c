/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger_chips.h"
#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"

#include <zephyr/devicetree.h>

void alt_charger_init(void)
{
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(charger_1)))) {
		CHG_ENABLE_ALTERNATE(0);
	}
}
DECLARE_HOOK(HOOK_INIT, alt_charger_init, HOOK_PRIO_POST_FIRST);
