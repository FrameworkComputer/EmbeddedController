/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include "driver/tcpm/ccgxxf.h"

#define CCGXXF_TCPC_COMPAT cypress_ccgxxf

#define TCPC_CONFIG_CCGXXF(id) \
	{                                              \
		.bus_type = EC_BUS_TYPE_I2C,           \
		.i2c_info = {                          \
			.port = I2C_PORT_BY_DEV(id),   \
			.addr_flags = DT_REG_ADDR(id), \
		},                                     \
		.drv = &ccgxxf_tcpm_drv,               \
		.flags = TCPC_FLAGS_TCPCI_REV2_0,      \
	},
