/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#if defined(CONFIG_PLATFORM_EC_CHARGER_BQ25720) || \
	defined(CONFIG_PLATFORM_EC_CHARGER_BQ25710)
#include "driver/charger/bq25710.h"

#define BQ25710_CHG_COMPAT ti_bq25710

#define CHG_CONFIG_BQ25710(id)                               \
	{                                                    \
		.i2c_port = I2C_PORT(DT_PHANDLE(id, port)),  \
		.i2c_addr_flags = BQ25710_SMBUS_ADDR1_FLAGS, \
		.drv = &bq25710_drv,                         \
	},
#endif
