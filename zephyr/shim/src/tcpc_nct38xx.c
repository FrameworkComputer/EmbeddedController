/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <devicetree.h>

#include "config.h"
#include "usbc/tcpc_nct38xx.h"

#define TCPC_PORT(id) DT_REG_ADDR(DT_PARENT(id))

#define GPIO_DEV_WITH_COMMA(id) DEVICE_DT_GET(DT_PHANDLE(id, gpio_dev)),

#define GPIO_DEV_BINDING(id)                        \
	COND_CODE_1(DT_NODE_HAS_PROP(id, gpio_dev), \
		    ([TCPC_PORT(id)] = GPIO_DEV_WITH_COMMA(id)), ())

/* NCT38XX GPIO device pool for binding the TCPC port and NCT38XX GPIO device */
static const struct device
	*nct38xx_gpio_devices[CONFIG_PLATFORM_EC_USB_PD_PORT_MAX_COUNT] = {
		DT_FOREACH_STATUS_OKAY(nuvoton_nct38xx, GPIO_DEV_BINDING)
	};

const struct device *nct38xx_get_gpio_device_from_port(const int port)
{
	if (port < 0 || port >= CONFIG_PLATFORM_EC_USB_PD_PORT_MAX_COUNT)
		return NULL;
	return nct38xx_gpio_devices[port];
}
