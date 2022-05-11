/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TCPC_NCT38XX_H
#define __CROS_EC_TCPC_NCT38XX_H

#include <zephyr/devicetree.h>
#include "driver/tcpm/nct38xx.h"

#define NCT38XX_TCPC_COMPAT nuvoton_nct38xx

#define TCPC_CONFIG_NCT38XX(id)                                               \
	{                                                                     \
		.bus_type = EC_BUS_TYPE_I2C,                                  \
		.i2c_info = {                                                 \
			.port = I2C_PORT(DT_PHANDLE(id, port)),               \
			.addr_flags = DT_STRING_UPPER_TOKEN(                  \
					id, i2c_addr_flags),                  \
		},                                                            \
		.drv = &nct38xx_tcpm_drv,                                     \
		.flags = DT_PROP(id, tcpc_flags),                             \
	},

/**
 * @brief Get the NCT38XX GPIO device from the TCPC port enumeration
 *
 * @param port The enumeration of TCPC port
 *
 * @return NULL if failed, otherwise a pointer to NCT38XX GPIO device
 */
const struct device *nct38xx_get_gpio_device_from_port(const int port);

#endif /* __CROS_EC_TCPC_NCT38XX_H */
