/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/raa489000.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAA489000_TCPC_COMPAT renesas_raa489000

/* clang-format off */
#define TCPC_CONFIG_RAA489000(id)                      \
	{                                              \
		.bus_type = EC_BUS_TYPE_I2C,           \
		.i2c_info = {                          \
			.port = I2C_PORT_BY_DEV(id),   \
			.addr_flags = DT_REG_ADDR(id), \
		},                                     \
		.drv = &raa489000_tcpm_drv,            \
		.flags = DT_PROP(id, tcpc_flags),      \
	}
/* clang-format on */

#ifdef __cplusplus
}
#endif
