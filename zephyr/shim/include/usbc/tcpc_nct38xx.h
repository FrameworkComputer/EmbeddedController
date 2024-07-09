/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TCPC_NCT38XX_H
#define __CROS_EC_TCPC_NCT38XX_H

#include "driver/tcpm/nct38xx.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NCT38XX_TCPC_COMPAT nuvoton_nct38xx_tcpc
#define NCT38XX_GPIO_COMPAT nuvoton_nct38xx_gpio

#ifdef CONFIG_MFD_NCT38XX
#define TCPC_MFD_PARENT(id) .mfd_parent = DEVICE_DT_GET(DT_PARENT(id)),
#else
#define TCPC_MFD_PARENT(id)
#endif

/* clang-format off */
#define TCPC_CONFIG_NCT38XX(id)                                                \
	{                                                                      \
		.bus_type = EC_BUS_TYPE_I2C,                                   \
		.i2c_info = {                                                  \
			.port = I2C_PORT_BY_DEV(DT_PARENT(id)),                \
			.addr_flags = DT_REG_ADDR(DT_PARENT(id)),              \
		},                                                             \
		.drv = &nct38xx_tcpm_drv,                                      \
		.flags = DT_PROP(id, tcpc_flags),                              \
		TCPC_MFD_PARENT(id)                                            \
		COND_CODE_1(CONFIG_PLATFORM_EC_TCPC_INTERRUPT,                 \
			(.irq_gpio = GPIO_DT_SPEC_GET_OR(id, irq_gpios, {}),   \
			 .rst_gpio = GPIO_DT_SPEC_GET_OR(id, rst_gpios, {})),  \
			(.alert_signal = COND_CODE_1(                          \
				DT_NODE_HAS_PROP(id, int_pin),                 \
				(GPIO_SIGNAL(DT_PHANDLE(id, int_pin))),        \
				(GPIO_LIMIT)))),                               \
	}
/* clang-format on */

/**
 * @brief Get the NCT38XX GPIO device from the TCPC port enumeration
 *
 * @param port The enumeration of TCPC port
 *
 * @return NULL if failed, otherwise a pointer to NCT38XX GPIO device
 */
const struct device *nct38xx_get_gpio_device_from_port(const int port);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_TCPC_NCT38XX_H */
