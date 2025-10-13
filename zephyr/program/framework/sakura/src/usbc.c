/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "hooks.h"
#include "cypress_pd_common.h"

static void board_change_cypd_init_state(void)
{
	pd_chip_config[PD_CHIP_0].state = CCG_STATE_NO_POWER;
	pd_chip_config[PD_CHIP_1].state = CCG_STATE_WAIT_STABLE;
}
DECLARE_HOOK(HOOK_INIT, board_change_cypd_init_state, HOOK_PRIO_DEFAULT);

void cypd_ccd_mode_control(void)
{
}
