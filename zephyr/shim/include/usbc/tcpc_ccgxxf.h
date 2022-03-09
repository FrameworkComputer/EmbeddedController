/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <devicetree.h>
#include "driver/tcpm/ccgxxf.h"

#define CCGXXF_TCPC_COMPAT cypress_ccgxxf

#define TCPC_CONFIG_CCGXXF(id)                                                \
	{                                                                     \
		.bus_type = EC_BUS_TYPE_I2C,                                  \
		.i2c_info = {                                                 \
			.port = I2C_PORT(DT_PHANDLE(id, port)),               \
			.addr_flags = DT_STRING_UPPER_TOKEN(                  \
					id, i2c_addr_flags),                  \
		},                                                            \
		.drv = &tcpci_tcpm_drv,                                       \
		.flags = TCPC_FLAGS_TCPCI_REV2_0,                             \
	},
