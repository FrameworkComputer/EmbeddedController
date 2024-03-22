/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "hooks.h"
#include "usbc/ppc.h"
#include "usbc/ppc_aoz1380.h"
#include "usbc/ppc_ktu1125.h"
#include "usbc/ppc_nx20p348x.h"
#include "usbc/ppc_rt1739.h"
#include "usbc/ppc_sn5s330.h"
#include "usbc/ppc_syv682x.h"
#include "usbc_ppc.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ppc, CONFIG_GPIO_LOG_LEVEL);

#define PPC_CHIP_ENTRY(usbc_id, ppc_id, config_fn) \
	[USBC_PORT_NEW(usbc_id)] = config_fn(ppc_id),

#define CHECK_COMPAT(compat, usbc_id, ppc_id, config_fn) \
	COND_CODE_1(DT_NODE_HAS_COMPAT(ppc_id, compat),  \
		    (PPC_CHIP_ENTRY(usbc_id, ppc_id, config_fn)), ())

/**
 * @param driver Tuple containing the PPC (compatible, config) pair.
 * @param nodes Tuple containing the (usbc_node_id, ppc_node_id) pair
 */
#define CHECK_COMPAT_HELPER(driver, nodes)                                     \
	CHECK_COMPAT(USBC_DRIVER_GET_COMPAT(driver), NODES_GET_USBC_ID(nodes), \
		     NODES_GET_PROP_ID(nodes), USBC_DRIVER_GET_CONFIG(driver))

#define PPC_CHIP_FIND(usbc_id, ppc_id)                                 \
	FOR_EACH_FIXED_ARG(CHECK_COMPAT_HELPER, (), (usbc_id, ppc_id), \
			   PPC_DRIVERS)

/*
 * This macro gets invoked for every driver in the PPC_DRIVERS list.
 * If the passed in ppc node contains the specified compat string, then
 * this macro returns 1.  Otherwise the macro returns nothing (EMPTY).
 */
#define PPC_HAS_COMPAT(compat, ppc) \
	IF_ENABLED(DT_NODE_HAS_COMPAT(ppc, compat), 1)

/*
 * Verify the compatible property of a PPC node is valid.
 *
 * Call PPC_HAS_COMPAT() for all PPC compatible strings listed in the
 * PPC_DRIVERS list.  If the resulting list is empty, then there was no
 * matching PPC driver found and this macro generates a build error.
 */
#define PPC_PROP_COMPATIBLE_VERIFY(ppc)                                        \
	IF_ENABLED(                                                            \
		IS_EMPTY(FOR_EACH_FIXED_ARG(PPC_HAS_COMPAT, (), ppc,           \
					    PPC_DRIVER_COMPATS)),              \
		(BUILD_ASSERT(0, "Invalid PPC compatible on node: " STRINGIFY( \
					 ppc));))

/* clang-format off */
#define PPC_CHIP_STUB(usbc_id) \
	[USBC_PORT_NEW(usbc_id)] = {},
/* clang-format on */

#define PPC_CHIP(usbc_id)                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, ppc),                     \
		    (PPC_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, ppc))), \
		    (PPC_CHIP_STUB(usbc_id)))

#define PPC_CHIP_VERIFY(usbc_id)                   \
	IF_ENABLED(DT_NODE_HAS_PROP(usbc_id, ppc), \
		   (PPC_PROP_COMPATIBLE_VERIFY(DT_PHANDLE(usbc_id, ppc))))

#define PPC_CHIP_ALT(usbc_id)                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, ppc_alt),                     \
		    (PPC_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, ppc_alt))), \
		    ())

/*
 * The PPC_CHIP_VERIFY macro expands to nothing when the PPC driver
 * compatible string is found in the PPC_DRIVER_COMPATS list.  Otherwise the
 * macro expands to a BUILD_ASSERT error.
 */
DT_FOREACH_STATUS_OKAY(named_usbc_port, PPC_CHIP_VERIFY)

struct ppc_config_t ppc_chips[] = { DT_FOREACH_STATUS_OKAY(named_usbc_port,
							   PPC_CHIP) };

unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

struct ppc_config_t ppc_chips_alt[] = { DT_FOREACH_STATUS_OKAY(named_usbc_port,
							       PPC_CHIP_ALT) };

#define PPC_ALT_DEFINITION(node_id, config_fn) \
	const struct ppc_config_t PPC_ALT_NAME_GET(node_id) = config_fn(node_id)

#define PPC_ALT_DEFINE(node_id, config_fn)          \
	COND_CODE_1(DT_PROP_OR(node_id, is_alt, 0), \
		    (PPC_ALT_DEFINITION(node_id, config_fn);), ())

/*
 * Define a global struct ppc_config_t for every PPC node in the tree with the
 * "is-alt" property set.
 */
DT_FOREACH_USBC_DRIVER_STATUS_OK_VARGS(PPC_ALT_DEFINE, PPC_DRIVERS)

BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == CONFIG_USB_PD_PORT_MAX_COUNT);

struct gpio_callback int_ppc_cb[CONFIG_USB_PD_PORT_MAX_COUNT];

static void ppc_int_gpio_callback(const struct device *dev,
				  struct gpio_callback *cb, uint32_t pins)
{
	/*
	 * Retrieve the array index from the callback pointer, and
	 * use that to get the port number.
	 */
	int port = cb - &int_ppc_cb[0];

	ppc_chips[port].drv->interrupt(port);
}

/*
 * Enable all ppc interrupts from devicetree bindings.
 * Check whether the callback is already installed, and if
 * not, init and add the callback before enabling the
 * interrupt.
 */
void ppc_enable_interrupt(void)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/*
		 * Check whether the interrupt pin has been configured
		 * by the devicetree.
		 */
		if (!ppc_chips[i].irq_gpio.port)
			continue;
		/*
		 * Check whether the gpio pin is ready
		 */
		if (!gpio_is_ready_dt(&ppc_chips[i].irq_gpio)) {
			LOG_ERR("tcpc port #%i interrupt not ready.", i);
			return;
		}
		/*
		 * TODO(b/267537103): Once named-gpios support is dropped,
		 * evaluate if this code should call gpio_pin_configure_dt()
		 *
		 * Check whether callback has been initialised
		 */
		if (!int_ppc_cb[i].handler) {
			/*
			 * Initialise and add the callback.
			 */
			gpio_init_callback(&int_ppc_cb[i],
					   ppc_int_gpio_callback,
					   BIT(ppc_chips[i].irq_gpio.pin));
			gpio_add_callback(ppc_chips[i].irq_gpio.port,
					  &int_ppc_cb[i]);
		}

		gpio_pin_interrupt_configure_dt(&ppc_chips[i].irq_gpio,
						GPIO_INT_EDGE_TO_ACTIVE);
	}
}

/*
 * priority set to POST_I2C + 1 so projects can make local edits to
 * ppc_chips as needed at POST_I2C before the interrupts are enabled.
 */
DECLARE_HOOK(HOOK_INIT, ppc_enable_interrupt, HOOK_PRIO_POST_I2C + 1);
