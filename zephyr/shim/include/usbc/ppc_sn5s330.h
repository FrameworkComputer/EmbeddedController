/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ppc/sn5s330_public.h"

#define SN5S330_COMPAT ti_sn5s330

#define PPC_CHIP_SN5S330(id)                                                 \
	{			                                             \
		.i2c_port = I2C_PORT(DT_PHANDLE(id, port)),                  \
		.i2c_addr_flags = DT_STRING_UPPER_TOKEN(id, i2c_addr_flags), \
		.drv = &sn5s330_drv                                          \
	},
