/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c.h"

__overridable int
board_allow_i2c_passthru(const struct i2c_cmd_desc_t *cmd_desc)
{
	return cmd_desc->port == I2C_PORT_BATTERY
#if DT_NODE_EXISTS(DT_NODELABEL(dp_bridge))
	       || (cmd_desc->port == I2C_PORT_BY_DEV(DT_NODELABEL(dp_bridge)))
#endif
		;
}
