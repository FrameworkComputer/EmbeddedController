/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c/i2c.h"
#include "i2c.h"

/* Lazor board specific i2c implementation */

#ifdef CONFIG_PLATFORM_EC_I2C_PASSTHRU_RESTRICTED
int board_allow_i2c_passthru(const struct i2c_cmd_desc_t *cmd_desc)
{
	return (i2c_get_device_for_port(cmd_desc->port) ==
		i2c_get_device_for_port(I2C_PORT_VIRTUAL_BATTERY));
}
#endif
