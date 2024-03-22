/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"

__override int board_allow_i2c_passthru(const struct i2c_cmd_desc_t *cmd_desc)
{
	/*
	 * AP tunneling to I2C is default-forbidden, but allowed for
	 * type-C and battery ports because these can be used to update TCPC or
	 * retimer firmware or specific battery access such as get battery
	 * vendor parameter. AP firmware separately sends a command to block
	 * tunneling to these ports after it's done updating chips.
	 */
	return false || (cmd_desc->port == I2C_PORT_BATTERY)
#if DT_NODE_EXISTS(DT_NODELABEL(tcpc_port0))
	       || (cmd_desc->port == I2C_PORT_BY_DEV(DT_NODELABEL(tcpc_port0)))
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(tcpc_port1))
	       || (cmd_desc->port == I2C_PORT_BY_DEV(DT_NODELABEL(tcpc_port1)))
#endif
		;
}
