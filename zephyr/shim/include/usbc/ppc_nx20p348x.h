/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ppc/nx20p348x_public.h"

#define NX20P348X_COMPAT nxp_nx20p348x

#define PPC_CHIP_NX20P348X(id)                                         \
	{ .i2c_port = I2C_PORT(DT_PHANDLE(id, port)),                  \
	  .i2c_addr_flags = DT_STRING_UPPER_TOKEN(id, i2c_addr_flags), \
	  .drv = &nx20p348x_drv },
