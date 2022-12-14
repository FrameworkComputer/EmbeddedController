/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tcpm/anx7447_public.h"

#include <zephyr/devicetree.h>

#define ANX7447_TCPC_COMPAT analogix_anx7447_tcpc

#define TCPC_CONFIG_ANX7447(id) \
	{                                                                      \
		.bus_type = EC_BUS_TYPE_I2C,                                   \
		.i2c_info = {                                                  \
			.port = I2C_PORT_BY_DEV(id),                           \
			.addr_flags = DT_REG_ADDR(id),                         \
		},                                                             \
		.drv = &anx7447_tcpm_drv,                                      \
		.flags = DT_PROP(id, tcpc_flags),                              \
		.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}),            \
	},
