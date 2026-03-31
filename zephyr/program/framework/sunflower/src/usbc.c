/* Copyright 2025 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "hooks.h"
#include "cypress_pd_common.h"

__override int board_perform_error_recovery_port(int port)
{
	int pd_port;

	if (port == PD_PORT_1 || port == PD_PORT_3)
		pd_port = port - 1;
	else if (port == PD_PORT_0 || port == PD_PORT_2)
		pd_port = port + 1;
	else
		pd_port = port;

	return pd_port;
}

static void board_change_cypd_init_state(void)
{
	pd_chip_config[PD_CHIP_0].state = CCG_STATE_WAIT_STABLE;
	pd_chip_config[PD_CHIP_1].state = CCG_STATE_WAIT_STABLE;
}
DECLARE_HOOK(HOOK_INIT, board_change_cypd_init_state, HOOK_PRIO_DEFAULT);

void cypd_ccd_mode_control(void)
{
}
