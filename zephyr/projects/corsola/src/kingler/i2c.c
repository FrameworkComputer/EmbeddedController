/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c/i2c.h"
#include "i2c.h"

/* Kingler board specific i2c implementation */

#ifdef CONFIG_PLATFORM_EC_I2C_PASSTHRU_RESTRICTED
int board_allow_i2c_passthru(int port)
{
	return (i2c_get_device_for_port(port) ==
		i2c_get_device_for_port(I2C_PORT_VIRTUAL_BATTERY));
}
#endif
