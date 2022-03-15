/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <devicetree.h>
#include "driver/tcpm/fusb302.h"

#define FUSB302_TCPC_COMPAT fairchild_fusb302

#define TCPC_CONFIG_FUSB302(id)                                               \
	{                                                                     \
		.bus_type = EC_BUS_TYPE_I2C,                                  \
		.i2c_info = {                                                 \
			.port = I2C_PORT(DT_PHANDLE(id, port)),               \
			.addr_flags = DT_STRING_UPPER_TOKEN(                  \
					id, i2c_addr_flags),                  \
		},                                                            \
		.drv = &fusb302_tcpm_drv,                                      \
	},
