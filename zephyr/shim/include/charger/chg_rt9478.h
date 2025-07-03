/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#ifdef CONFIG_PLATFORM_EC_CHARGER_RT9478
#include "driver/charger/rt9478.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT9478_CHG_COMPAT richtek_rt9478

#define CHG_CONFIG_RT9478(id)                      \
	{                                          \
		.i2c_port = I2C_PORT_BY_DEV(id),   \
		.i2c_addr_flags = DT_REG_ADDR(id), \
		.drv = &rt9478_drv,                \
	},

#ifdef __cplusplus
}
#endif

#endif
