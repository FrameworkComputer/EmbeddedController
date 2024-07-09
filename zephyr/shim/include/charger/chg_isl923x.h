/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/charger/isl923x_public.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ISL923X_CHG_COMPAT intersil_isl923x
#define ISL923X_EMUL_COMPAT cros_isl923x_emul

#define CHG_CONFIG_ISL923X(id)                     \
	{                                          \
		.i2c_port = I2C_PORT_BY_DEV(id),   \
		.i2c_addr_flags = DT_REG_ADDR(id), \
		.drv = &isl923x_drv,               \
	},

#ifdef __cplusplus
}
#endif
