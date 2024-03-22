/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#ifndef __ZEPHYR_SHIM_TCPC_RT1718S_EMUL_H
#define __ZEPHYR_SHIM_TCPC_RT1718S_EMUL_H

#include "driver/tcpm/rt1718s_public.h"

#include <zephyr/devicetree.h>

#define RT1718S_EMUL_COMPAT cros_rt1718s_tcpc_emul

/* clang-format off */
#define TCPC_CONFIG_RT1718S_EMUL(id)                                           \
	{                                                                      \
		.bus_type = EC_BUS_TYPE_I2C,                                   \
		.i2c_info = {                                                  \
			.port = I2C_PORT_BY_DEV(id),                           \
			.addr_flags = DT_REG_ADDR(id),                         \
		},                                                             \
		.drv = &rt1718s_tcpm_drv,                                      \
		.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}),            \
	}
/* clang-format on */

#endif /* __ZEPHYR_SHIM_TPCP_RT1718S_EMUL_H */
