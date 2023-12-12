/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "hooks.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc/tcpc_anx7447.h"
#include "usbc/tcpc_anx7447_emul.h"
#include "usbc/tcpc_ccgxxf.h"
#include "usbc/tcpc_fusb302.h"
#include "usbc/tcpc_generic_emul.h"
#include "usbc/tcpc_it8xxx2.h"
#include "usbc/tcpc_nct38xx.h"
#include "usbc/tcpc_ps8xxx.h"
#include "usbc/tcpc_ps8xxx_emul.h"
#include "usbc/tcpc_raa489000.h"
#include "usbc/tcpc_rt1715.h"
#include "usbc/tcpc_rt1718s.h"
#include "usbc/tcpc_rt1718s_emul.h"
#include "usbc/tcpci.h"
#include "usbc/utils.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(tcpc, CONFIG_GPIO_LOG_LEVEL);

#define HAS_TCPC_PROP(usbc_id) \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, tcpc), (|| 1), ())

#define DT_HAS_TCPC (0 DT_FOREACH_STATUS_OKAY(named_usbc_port, HAS_TCPC_PROP))

#if DT_HAS_TCPC

#define TCPC_CHIP_ENTRY(usbc_id, tcpc_id, config_fn) \
	[USBC_PORT_NEW(usbc_id)] = config_fn(tcpc_id),

#define CHECK_COMPAT(compat, usbc_id, tcpc_id, config_fn) \
	COND_CODE_1(DT_NODE_HAS_COMPAT(tcpc_id, compat),  \
		    (TCPC_CHIP_ENTRY(usbc_id, tcpc_id, config_fn)), ())

/**
 * @param driver Tuple containing the TCPC (compatible, config) pair.
 * @param nodes Tuple containing the (usbc_node_id, tcpc_node_id) pair
 */
#define CHECK_COMPAT_HELPER(driver, nodes)                                     \
	CHECK_COMPAT(USBC_DRIVER_GET_COMPAT(driver), NODES_GET_USBC_ID(nodes), \
		     NODES_GET_PROP_ID(nodes), USBC_DRIVER_GET_CONFIG(driver))

#define TCPC_CHIP_FIND(usbc_id, tcpc_id)                                \
	FOR_EACH_FIXED_ARG(CHECK_COMPAT_HELPER, (), (usbc_id, tcpc_id), \
			   TCPC_DRIVERS)

/*
 * This macro gets invoked for every driver in the TCPC_DRIVERS list.
 * If the passed in tcpc node contains the specified compat string, then
 * this macro returns 1.  Otherwise the macro returns nothing (EMPTY).
 */
#define TCPC_HAS_COMPAT(compat, tcpc) \
	IF_ENABLED(DT_NODE_HAS_COMPAT(tcpc, compat), 1)

/*
 * Verify the compatible property of a TCPC node is valid.
 *
 * Call TCPC_HAS_COMPAT() for all TCPC compatible strings listed in the
 * TCPC_DRIVERS list.  If the resulting list is empty, then there was no
 * matching TCPC driver found and this macro generates a build error.
 */
#define TCPC_PROP_COMPATIBLE_VERIFY(tcpc)                                     \
	IF_ENABLED(IS_EMPTY(FOR_EACH_FIXED_ARG(TCPC_HAS_COMPAT, (), tcpc,     \
					       TCPC_DRIVER_COMPATS)),         \
		   (BUILD_ASSERT(                                             \
			    0, "Invalid TCPC compatible on node: " STRINGIFY( \
				       tcpc));))

/* clang-format off */
#define TCPC_CHIP_STUB(usbc_id) \
	[USBC_PORT_NEW(usbc_id)] = {},
/* clang-format on */

#define TCPC_CHIP(usbc_id)                                                \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, tcpc),                      \
		    (TCPC_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, tcpc))), \
		    (TCPC_CHIP_STUB(usbc_id)))

#define TCPC_CHIP_VERIFY(usbc_id)                   \
	IF_ENABLED(DT_NODE_HAS_PROP(usbc_id, tcpc), \
		   (TCPC_PROP_COMPATIBLE_VERIFY(DT_PHANDLE(usbc_id, tcpc))))

#define MAYBE_CONST \
	COND_CODE_1(CONFIG_PLATFORM_EC_USB_PD_TCPC_RUNTIME_CONFIG, (), (const))

/*
 * The TCPC_CHIP_VERIFY macro expands to nothing when the TCPC driver
 * compatible string is found in the TCPC_DRIVER_COMPATS list. Otherwise
 * the macro expands to a BUILD_ASSERT error.
 */
DT_FOREACH_STATUS_OKAY(named_usbc_port, TCPC_CHIP_VERIFY)

/* Type C Port Controllers */
MAYBE_CONST struct tcpc_config_t tcpc_config[] = { DT_FOREACH_STATUS_OKAY(
	named_usbc_port, TCPC_CHIP) };

#define TCPC_ALT_DEFINITION(node_id, config_fn)                 \
	const struct tcpc_config_t TCPC_ALT_NAME_GET(node_id) = \
		config_fn(node_id)

#define TCPC_ALT_DEFINE(node_id, config_fn)         \
	COND_CODE_1(DT_PROP_OR(node_id, is_alt, 0), \
		    (TCPC_ALT_DEFINITION(node_id, config_fn);), ())

/*
 * Define a struct tcpc_config_t for every TCPC node in the tree with the
 * "is-alt" property set.
 */
DT_FOREACH_USBC_DRIVER_STATUS_OK_VARGS(TCPC_ALT_DEFINE, TCPC_DRIVERS)

#ifdef CONFIG_PLATFORM_EC_TCPC_INTERRUPT

BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == CONFIG_USB_PD_PORT_MAX_COUNT);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	uint16_t alert_mask[] = { PD_STATUS_TCPC_ALERT_0,
				  PD_STATUS_TCPC_ALERT_1,
				  PD_STATUS_TCPC_ALERT_2,
				  PD_STATUS_TCPC_ALERT_3 };

	/*
	 * Check which port has the ALERT line set and ignore if that TCPC has
	 * its reset line active.
	 */
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/*
		 * if the interrupt port exists and the interrupt is active
		 */
		if (tcpc_config[i].irq_gpio.port &&
		    gpio_pin_get_dt(&tcpc_config[i].irq_gpio))
			/*
			 * if the reset line does not exist or exists but is
			 * not active.
			 */
			if (!tcpc_config[i].rst_gpio.port ||
			    !gpio_pin_get_dt(&tcpc_config[i].rst_gpio))
				status |= alert_mask[i];
	}

	return status;
}

struct gpio_callback int_tcpc_cb[CONFIG_USB_PD_PORT_MAX_COUNT];

static void tcpc_int_gpio_callback(const struct device *dev,
				   struct gpio_callback *cb, uint32_t pins)
{
	/*
	 * Retrieve the array index from the callback pointer, and
	 * use that to get the port number.
	 */
	int port = cb - &int_tcpc_cb[0];

	schedule_deferred_pd_interrupt(port);
}

/*
 * Enable all tcpc interrupts from devicetree bindings.
 * Check whether the callback is already installed, and if
 * not, init and add the callback before enabling the
 * interrupt.
 */
void tcpc_enable_interrupt(void)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/*
		 * Check whether the interrupt pin has been configured
		 * by the devicetree.
		 */
		if (!tcpc_config[i].irq_gpio.port)
			continue;
		/*
		 * Check whether the gpio pin is ready
		 */
		if (!gpio_is_ready_dt(&tcpc_config[i].irq_gpio)) {
			LOG_ERR("tcpc port #%i interrupt not ready.", i);
			return;
		}
		/*
		 * TODO(b/267537103): Once named-gpios support is dropped,
		 * evaluate if this code should call gpio_pin_configure_dt()
		 *
		 * Check whether callback has been initialised
		 */
		if (!int_tcpc_cb[i].handler) {
			/*
			 * Initialise and add the callback.
			 */
			gpio_init_callback(&int_tcpc_cb[i],
					   tcpc_int_gpio_callback,
					   BIT(tcpc_config[i].irq_gpio.pin));
			gpio_add_callback(tcpc_config[i].irq_gpio.port,
					  &int_tcpc_cb[i]);
		}

		gpio_pin_interrupt_configure_dt(&tcpc_config[i].irq_gpio,
						GPIO_INT_EDGE_TO_ACTIVE);
	}
}
/*
 * priority set to POST_I2C + 1 so projects can make local edits to
 * tcpc_config as needed at POST_I2C before the interrupts are enabled.
 */
DECLARE_HOOK(HOOK_INIT, tcpc_enable_interrupt, HOOK_PRIO_POST_I2C + 1);

#else /* CONFIG_PLATFORM_EC_TCPC_INTERRUPT */

/* TCPC GPIO Interrupt Handlers */
void tcpc_alert_event(enum gpio_signal signal)
{
	for (int i = 0; i < ARRAY_SIZE(tcpc_config); i++) {
		/* No alerts if the alert pin is not set in the devicetree */
		if (tcpc_config[i].alert_signal == GPIO_LIMIT) {
			continue;
		}

		if (signal == tcpc_config[i].alert_signal) {
			schedule_deferred_pd_interrupt(i);
			break;
		}
	}
}

#endif /* CONFIG_PLATFORM_EC_TCPC_INTERRUPT */
#endif /* DT_HAS_COMPAT_STATUS_OKAY */
