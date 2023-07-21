/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "usbc/tcpc_nct38xx.h"
#include "usbc/utils.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#define GPIO_DEV_WITH_COMMA(id) DEVICE_DT_GET(DT_PHANDLE(id, gpio_dev)),

#define GPIO_DEV_BINDING(usbc_id, tcpc_id)                                     \
	COND_CODE_1(DT_NODE_HAS_PROP(tcpc_id, gpio_dev),                       \
		    ([USBC_PORT_NEW(usbc_id)] = GPIO_DEV_WITH_COMMA(tcpc_id)), \
		    ())

#define NCT38XX_CHECK(usbc_id, tcpc_id)                               \
	COND_CODE_1(DT_NODE_HAS_COMPAT(tcpc_id, NCT38XX_TCPC_COMPAT), \
		    (GPIO_DEV_BINDING(usbc_id, tcpc_id)), ())

#define NCT38XX_GPIO(usbc_id)                        \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, tcpc), \
		    (NCT38XX_CHECK(usbc_id, DT_PHANDLE(usbc_id, tcpc))), ())

/* NCT38XX GPIO device pool for binding the TCPC port and NCT38XX GPIO device */
static const struct device *nct38xx_gpio_devices[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	DT_FOREACH_STATUS_OKAY(named_usbc_port, NCT38XX_GPIO)
};

const struct device *nct38xx_get_gpio_device_from_port(const int port)
{
	if (port < 0 || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return NULL;
	return nct38xx_gpio_devices[port];
}
