/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "common.h"
#include "driver/charger/rt9490.h"
#include "hooks.h"
#include "i2c.h"

static void board_rt9490_workaround(void)
{
	/* disable ibus ADC and vbus ADC */
	if (system_get_board_version() == 0) {
		i2c_write8(chg_chips[CHARGER_SOLO].i2c_port,
			   chg_chips[CHARGER_SOLO].i2c_addr_flags, 0x2f, 0xa1);
	}
}
DECLARE_HOOK(HOOK_INIT, board_rt9490_workaround, HOOK_PRIO_DEFAULT);
