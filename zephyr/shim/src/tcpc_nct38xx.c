/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "usbc/tcpc_nct38xx.h"
#include "usbc/utils.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#define GPIO_DEV_PROP gpio_dev

#define GPIO_DEV_WITH_COMMA(id) DEVICE_DT_GET(DT_PHANDLE(id, GPIO_DEV_PROP)),

#define GPIO_DEV_BINDING(usbc_id, tcpc_id)                                     \
	COND_CODE_1(DT_NODE_HAS_PROP(tcpc_id, GPIO_DEV_PROP),                  \
		    ([USBC_PORT_NEW(usbc_id)] = GPIO_DEV_WITH_COMMA(tcpc_id)), \
		    ())

#define NCT38XX_CHECK(usbc_id, tcpc_id)                               \
	COND_CODE_1(DT_NODE_HAS_COMPAT(tcpc_id, NCT38XX_TCPC_COMPAT), \
		    (GPIO_DEV_BINDING(usbc_id, tcpc_id)), ())

#define NCT38XX_GPIO(usbc_id)                        \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, tcpc), \
		    (NCT38XX_CHECK(usbc_id, DT_PHANDLE(usbc_id, tcpc))), ())

/* Compile-time checks. */
/* Validate the GPIO device referenced by the TCPC matches our GPIO compat. */
/* clang-format off */
#define NCT38XX_TCPC_GPIO_VALIDATE(usbc_id, tcpc_id, gpio_id)                  \
	BUILD_ASSERT(DT_NODE_HAS_COMPAT(gpio_id, NCT38XX_GPIO_COMPAT),         \
		DT_NODE_PATH(gpio_id) " referenced by property "               \
		STRINGIFY(GPIO_DEV_PROP) " of " DT_NODE_PATH(tcpc_id)          \
		" is not compatible with " STRINGIFY(NCT38XX_GPIO_COMPAT));
/* clang-format on */

/* TCPC device validation. */
#define NCT38XX_TCPC_VALIDATE(usbc_id, tcpc_id)                              \
	COND_CODE_1(DT_NODE_HAS_PROP(tcpc_id, GPIO_DEV_PROP),                \
		    (NCT38XX_TCPC_GPIO_VALIDATE(usbc_id, tcpc_id,            \
						DT_PHANDLE(tcpc_id,          \
							   GPIO_DEV_PROP))), \
		    ())

/* Base validation macro. */
#define NCT38XX_VALIDATE(usbc_id)                                            \
	COND_CODE_1(                                                         \
		DT_NODE_HAS_PROP(usbc_id, tcpc),                             \
		(NCT38XX_TCPC_VALIDATE(usbc_id, DT_PHANDLE(usbc_id, tcpc))), \
		())

/* NCT38XX GPIO device pool for binding the TCPC port and NCT38XX GPIO device */
static const struct device *nct38xx_gpio_devices[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	DT_FOREACH_STATUS_OKAY(named_usbc_port, NCT38XX_GPIO)
};

DT_FOREACH_STATUS_OKAY(named_usbc_port, NCT38XX_VALIDATE)

const struct device *nct38xx_get_gpio_device_from_port(const int port)
{
	if (port < 0 || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return NULL;
	return nct38xx_gpio_devices[port];
}
