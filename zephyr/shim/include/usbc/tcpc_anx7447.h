/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include "tcpm/anx7447_public.h"

#define ANX7447_TCPC_COMPAT anologix_anx7447_tcpc

#define TCPC_CONFIG_ANX7447(id) \
	{                                                                     \
		.bus_type = EC_BUS_TYPE_I2C,                                  \
		.i2c_info = {                                                 \
			.port = I2C_PORT(DT_PHANDLE(id, port)),               \
			.addr_flags = DT_STRING_UPPER_TOKEN(                  \
					id, i2c_addr_flags),                  \
		},                                                            \
		.drv = &anx7447_tcpm_drv,                                     \
		.flags = DT_PROP(id, tcpc_flags),                             \
	},
