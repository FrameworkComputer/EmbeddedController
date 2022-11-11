/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/fusb302.h"

#include <zephyr/devicetree.h>

#define FUSB302_TCPC_COMPAT fairchild_fusb302

#define TCPC_CONFIG_FUSB302(id) \
	{                                                                      \
		.bus_type = EC_BUS_TYPE_I2C,                                   \
		.i2c_info = {                                                  \
			.port = I2C_PORT_BY_DEV(id),                           \
			.addr_flags = DT_REG_ADDR(id),                         \
		},                                                             \
		.drv = &fusb302_tcpm_drv,                                      \
		.alert_signal = COND_CODE_1(DT_NODE_HAS_PROP(id, int_pin),     \
			(GPIO_SIGNAL(DT_PHANDLE(id, int_pin))),                \
			(GPIO_LIMIT)),                                         \
	},
