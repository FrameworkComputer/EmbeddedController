/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include "tcpm/rt1718s_public.h"

#define RT1718S_TCPC_COMPAT richtek_rt1718s_tcpc

/*
 * Currently, the clang-format will force the back-slash at col:81. Enable
 * format after we fix the issue.
 */
/* clang-format off */
#define TCPC_CONFIG_RT1718S(id)                                               \
	{                                                                     \
		.bus_type = EC_BUS_TYPE_I2C,                                  \
		.i2c_info = {                                                 \
			.port = I2C_PORT(DT_PHANDLE(id, port)),               \
			.addr_flags = DT_STRING_UPPER_TOKEN(                  \
					id, i2c_addr_flags),                  \
		},                                                            \
		.drv = &rt1718s_tcpm_drv,                                     \
		.flags = DT_PROP(id, tcpc_flags),                             \
	},
/* clang-format on */
