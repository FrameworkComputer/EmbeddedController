/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#if defined(CONFIG_PLATFORM_EC_CHARGER_BQ25720) || \
	defined(CONFIG_PLATFORM_EC_CHARGER_BQ25710)
#include "driver/charger/bq25710.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BQ25710_CHG_COMPAT ti_bq25710

#define CHG_CONFIG_BQ25710(id)                     \
	{                                          \
		.i2c_port = I2C_PORT_BY_DEV(id),   \
		.i2c_addr_flags = DT_REG_ADDR(id), \
		.drv = &bq25710_drv,               \
	},

#ifdef __cplusplus
}
#endif

#endif
