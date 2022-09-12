/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/ppc/rt1739.h"

#define RT1739_PPC_COMPAT richtek_rt1739

#define PPC_CHIP_RT1739(id)                                               \
	{                                                                 \
		.i2c_port = I2C_PORT_BY_DEV(id),                          \
		.i2c_addr_flags = DT_REG_ADDR(id),                        \
		.drv = &rt1739_ppc_drv,                                   \
		.frs_en = COND_CODE_1(DT_NODE_HAS_PROP(id, irq),          \
				      (GPIO_SIGNAL(DT_PHANDLE(id, irq))), \
				      (0)),                               \
	},
