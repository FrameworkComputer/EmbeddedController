/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include "driver/tcpm/ps8xxx_public.h"

#define PS8XXX_EMUL_COMPAT cros_ps8xxx_emul

#define TCPC_CONFIG_PS8XXX_EMUL(id) \
	{                                              \
		.bus_type = EC_BUS_TYPE_I2C,           \
		.i2c_info = {                          \
			.port = I2C_PORT_BY_DEV(id),   \
			.addr_flags = DT_REG_ADDR(id), \
		},                                     \
		.drv = &ps8xxx_tcpm_drv,               \
	},
