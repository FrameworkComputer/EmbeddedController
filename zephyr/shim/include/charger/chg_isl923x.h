/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <devicetree.h>
#include "driver/charger/isl923x_public.h"

#define ISL923X_CHG_COMPAT intersil_isl923x

#define CHG_CONFIG_ISL923X(id)                              \
	{                                                   \
		.i2c_port = I2C_PORT(DT_PHANDLE(id, port)), \
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,       \
		.drv = &isl923x_drv,                        \
	},
