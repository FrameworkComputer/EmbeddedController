/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/fusb302.h"
#include "usbc/utils.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FUSB302_TCPC_COMPAT fairchild_fusb302

/* clang-format off */
#define TCPC_CONFIG_FUSB302(id)                                                \
	{                                                                      \
		.bus_type = EC_BUS_TYPE_I2C,                                   \
		.i2c_info = {                                                  \
			.port = I2C_PORT_BY_DEV(id),                           \
			.addr_flags = DT_REG_ADDR(id),                         \
		},                                                             \
		.drv = &fusb302_tcpm_drv,                                      \
		COND_CODE_1(CONFIG_PLATFORM_EC_TCPC_INTERRUPT,                 \
			(.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}),   \
			 .rst_gpio = GPIO_DT_SPEC_GET_OR(id, rst_gpios, {})),  \
			(.alert_signal = COND_CODE_1(                          \
				DT_NODE_HAS_PROP(id, int_pin),                 \
				(GPIO_SIGNAL(DT_PHANDLE(id, int_pin))),        \
				(GPIO_LIMIT)))),                               \
	}
/* clang-format on */

DT_FOREACH_STATUS_OKAY(FUSB302_TCPC_COMPAT,
		       TCPC_VERIFY_NO_FLAGS_ACTIVE_ALERT_HIGH)

#ifdef __cplusplus
}
#endif