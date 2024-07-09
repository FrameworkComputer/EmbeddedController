/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ppc/syv682x_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYV682X_COMPAT silergy_syv682x
#define SYV682X_EMUL_COMPAT zephyr_syv682x_emul

#define PPC_CHIP_SYV682X(id)                                              \
	{                                                                 \
		.i2c_port = I2C_PORT_BY_DEV(id),                          \
		.i2c_addr_flags = DT_REG_ADDR(id), .drv = &syv682x_drv,   \
		.frs_en = COND_CODE_1(                                    \
			DT_NODE_HAS_PROP(id, frs_en_gpio),                \
			(GPIO_SIGNAL(DT_PHANDLE(id, frs_en_gpio))), (0)), \
		.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}),       \
	}

#ifdef __cplusplus
}
#endif
