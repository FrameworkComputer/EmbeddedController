/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Herobrine baseboard-specific configuration */

#include "i2c.h"

int board_allow_i2c_passthru(const struct i2c_cmd_desc_t *cmd_desc)
{
	return (cmd_desc->port == I2C_PORT_VIRTUAL_BATTERY);
}
