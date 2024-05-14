/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/rt1715.h"
#include "usbc/utils.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT1715_TCPC_COMPAT richtek_rt1715_tcpc

/* clang-format off */
#define TCPC_CONFIG_RT1715(id)                                                 \
	{                                                                      \
		.bus_type = EC_BUS_TYPE_I2C,                                   \
		.i2c_info = {                                                  \
			.port = I2C_PORT_BY_DEV(id),                           \
			.addr_flags = DT_REG_ADDR(id),                         \
		},                                                             \
		.drv = &rt1715_tcpm_drv,                                       \
		.flags = DT_PROP(id, tcpc_flags),                              \
		.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}), \
	}
/* clang-format on */

DT_FOREACH_STATUS_OKAY(RT1715_TCPC_COMPAT,
		       TCPC_VERIFY_NO_FLAGS_ACTIVE_ALERT_HIGH)

#ifdef __cplusplus
}
#endif
