/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "driver/charger/rt9490.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"

/* work around for IBUS ADC unstable issue */
void board_rt9490_workaround(void)
{
	if (system_get_board_version() != 0)
		return;

	i2c_update8(chg_chips[CHARGER_SOLO].i2c_port,
		    chg_chips[CHARGER_SOLO].i2c_addr_flags,
		    RT9490_REG_ADC_CHANNEL0,
		    RT9490_VSYS_ADC_DIS,
		    MASK_SET);

	/* undocumented registers... */
	i2c_write8(chg_chips[CHARGER_SOLO].i2c_port,
		   chg_chips[CHARGER_SOLO].i2c_addr_flags,
		   0xF1, 0x69);
	i2c_write8(chg_chips[CHARGER_SOLO].i2c_port,
		   chg_chips[CHARGER_SOLO].i2c_addr_flags,
		   0xF2, 0x96);
	i2c_write8(chg_chips[CHARGER_SOLO].i2c_port,
		   chg_chips[CHARGER_SOLO].i2c_addr_flags,
		   0x52, 0xC4);

	i2c_update8(chg_chips[CHARGER_SOLO].i2c_port,
		    chg_chips[CHARGER_SOLO].i2c_addr_flags,
		    RT9490_REG_ADC_CHANNEL0,
		    RT9490_VSYS_ADC_DIS,
		    MASK_CLR);
}
DECLARE_HOOK(HOOK_INIT, board_rt9490_workaround, HOOK_PRIO_DEFAULT);
