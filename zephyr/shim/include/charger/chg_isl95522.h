/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/charger/isl95522_public.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ISL95522_CHG_COMPAT intersil_isl95522

#define CHG_CONFIG_ISL95522(id)                                     \
	{                                                           \
		.i2c_port = I2C_PORT_BY_DEV(id),                    \
		.i2c_addr_flags = DT_REG_ADDR(id),                  \
		.drv = &isl95522_drv,                               \
		.minimum_charging_mv =                              \
			DT_PROP_OR(id, minimum_charging_mv,         \
				   CHARGER_NO_MINIMUM_CHARGING_MV), \
	},

#ifdef __cplusplus
}
#endif
