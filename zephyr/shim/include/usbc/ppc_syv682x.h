/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ppc/syv682x_public.h"

#define SYV682X_COMPAT silergy_syv682x

#define PPC_CHIP_SYV682X(id)                                                 \
	{                                                                    \
		.i2c_port = I2C_PORT(DT_PHANDLE(id, port)),                  \
		.i2c_addr_flags = DT_STRING_UPPER_TOKEN(id, i2c_addr_flags), \
		.drv = &syv682x_drv,                                         \
	},
