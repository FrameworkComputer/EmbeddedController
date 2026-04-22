/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "hooks.h"
#include "cypress_pd_common.h"

#define CCG_I2C_CHIP0 0x08

static void board_change_cypd_init_state(void)
{
	pd_chip_config[PD_CHIP_0].addr_flags = CCG_I2C_CHIP0 |
		I2C_FLAG_ADDR16_LITTLE_ENDIAN;

	pd_chip_config[PD_CHIP_0].state = CCG_STATE_WAIT_STABLE;
	pd_chip_config[PD_CHIP_1].state = CCG_STATE_WAIT_STABLE;
}
DECLARE_HOOK(HOOK_INIT, board_change_cypd_init_state, HOOK_PRIO_DEFAULT);
