/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <devicetree.h>
#include "driver/charger/rt9490.h"

#define RT9490_CHG_COMPAT richtek_rt9490

#define CHG_CONFIG_RT9490(id)                               \
	{                                                   \
		.i2c_port = I2C_PORT(DT_PHANDLE(id, port)), \
		.i2c_addr_flags = RT9490_ADDR_FLAGS,        \
		.drv = &rt9490_drv,                         \
	},
