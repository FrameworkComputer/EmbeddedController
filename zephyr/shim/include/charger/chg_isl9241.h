/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <devicetree.h>
#include "driver/charger/isl9241_public.h"

#define ISL9241_CHG_COMPAT intersil_isl9241

#define CHG_CONFIG_ISL9241(id)                              \
	{                                                   \
		.i2c_port = I2C_PORT(DT_PHANDLE(id, port)), \
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,       \
		.drv = &isl9241_drv,                        \
	},
