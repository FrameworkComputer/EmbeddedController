/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>

#include "driver/tcpm/tcpci.h"

#define TCPCI_COMPAT cros_ec_tcpci

#define TCPC_CONFIG_TCPCI(id)                                                 \
	{                                                                     \
		.bus_type = EC_BUS_TYPE_I2C,                                  \
		.i2c_info = {                                                 \
			.port = I2C_PORT(DT_PHANDLE(id, port)),               \
			.addr_flags = DT_PROP(id, i2c_addr_flags),            \
		},                                                            \
		.drv = &tcpci_tcpm_drv,                                       \
	},
