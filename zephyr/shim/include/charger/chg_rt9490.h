/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/charger/rt9490.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT9490_CHG_COMPAT richtek_rt9490
#define RT9490_EMUL_COMPAT zephyr_rt9490_emul

#define CHG_CONFIG_RT9490(id)                      \
	{                                          \
		.i2c_port = I2C_PORT_BY_DEV(id),   \
		.i2c_addr_flags = DT_REG_ADDR(id), \
		.drv = &rt9490_drv,                \
	},

#ifdef __cplusplus
}
#endif
