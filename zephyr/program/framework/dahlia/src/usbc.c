/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "chipset.h"
#include "console.h"
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

static void board_request_5v_rdo(void)
{
	int batt_state = 0;

	(void)battery_status(&batt_state);

	/* If no battery or battery is not full, do not reduce VBus to 5V */
	if (!(batt_state & SB_STATUS_FULLY_CHARGED))
		return;

	for (int port = 0; port < PD_PORT_COUNT; port++)
		cypd_board_set_port_rdo(port, SELECT_SINK_RDO_5V);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN_COMPLETE, board_request_5v_rdo, HOOK_PRIO_DEFAULT);

static void board_request_highest_rdo(void)
{
	for (int port = 0; port < PD_PORT_COUNT; port++)
		cypd_board_set_port_rdo(port, SELECT_SINK_RDO_HIGHEST);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_request_highest_rdo, HOOK_PRIO_DEFAULT);

static void board_request_rdo(void)
{
	int rdo;
	int batt_state = 0;

	(void)battery_status(&batt_state);

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
		if (batt_state & SB_STATUS_FULLY_CHARGED)
			rdo = SELECT_SINK_RDO_5V;
		else
			rdo = SELECT_SINK_RDO_HIGHEST;

		for (int port = 0; port < PD_PORT_COUNT; port++)
			cypd_board_set_port_rdo(port, rdo);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_request_rdo, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, board_request_rdo, HOOK_PRIO_DEFAULT);
