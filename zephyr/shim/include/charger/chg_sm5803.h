/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <devicetree.h>
#include "driver/charger/sm5803.h"

#define SM5803_CHG_COMPAT siliconmitus_sm5803

#define CHG_CONFIG_SM5803(id)                                \
	{                                                    \
		.i2c_port = I2C_PORT(DT_PHANDLE(id, port)),  \
		.i2c_addr_flags = SM5803_ADDR_CHARGER_FLAGS, \
		.drv = &sm5803_drv,                          \
	},
