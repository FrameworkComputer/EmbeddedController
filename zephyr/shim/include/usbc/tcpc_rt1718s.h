/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include "tcpm/rt1718s_public.h"

#define RT1718S_TCPC_COMPAT richtek_rt1718s_tcpc

#define TCPC_CONFIG_RT1718S(id) \
	{                                              \
		.bus_type = EC_BUS_TYPE_I2C,           \
		.i2c_info = {                          \
			.port = I2C_PORT_BY_DEV(id),   \
			.addr_flags = DT_REG_ADDR(id), \
		},                                     \
		.drv = &rt1718s_tcpm_drv,              \
		.flags = DT_PROP(id, tcpc_flags),      \
	},
