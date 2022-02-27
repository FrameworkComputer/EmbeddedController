/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <init.h>
#include <kernel.h>
#include <logging/log.h>

#ifdef __REQUIRE_ZEPHYR_GPIOS__
#undef __REQUIRE_ZEPHYR_GPIOS__
#endif
#include "gpio.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "cros_version.h"

LOG_MODULE_REGISTER(gpio_int, LOG_LEVEL_ERR);

/*
 * Structure containing the read-only configuration data for an
 * interrupt, such as the initial flags and the handler vector.
 * The RW callback data is kept in a separate array.
 */
struct gpio_int_config {
	void (*handler)(enum gpio_signal); /* Handler to call */
	gpio_flags_t flags; /* Flags */
	const struct device *port; /* GPIO device */
	gpio_pin_t pin; /* GPIO pin */
	enum gpio_signal signal; /* Signal associated with interrupt */
};

/*
 * Verify there is only a single interrupt node.
 */
#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_gpio_interrupts)
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(cros_ec_gpio_interrupts) == 1,
	     "Only one node for cros_ec_gpio_interrupts is allowed");
#endif

/*
 * Shorthand to get the node containing the interrupt DTS.
 */
#define DT_IRQ_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_gpio_interrupts)

/*
 * Declare all the external handlers.
 */

#define INT_HANDLER_DECLARE(id) \
	extern void DT_STRING_TOKEN(id, handler)(enum gpio_signal);

#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_gpio_interrupts)
DT_FOREACH_CHILD(DT_IRQ_NODE, INT_HANDLER_DECLARE)
#endif

#undef INT_HANDLER_DECLARE

/*
 * Create an array of callbacks. This is separate from the
 * configuration so that the writable data is in BSS.
 */
struct gpio_callback int_cb_data[GPIO_INT_COUNT];

/*
 * Create an instance of a gpio_int_config structure from a DTS node
 */

#define INT_CONFIG_ENTRY(id, irq_pin)                                \
	{                                                            \
		.handler = DT_STRING_TOKEN(id, handler),             \
		.flags = DT_PROP(id, flags),                         \
		.port = DEVICE_DT_GET(DT_GPIO_CTLR(irq_pin, gpios)), \
		.pin = DT_GPIO_PIN(irq_pin, gpios),                  \
		.signal = GPIO_SIGNAL(irq_pin),                      \
	},

#define INT_CONFIG_FROM_NODE(id) INT_CONFIG_ENTRY(id, DT_PROP(id, irq_pin))

#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_gpio_interrupts)
/*
 * Create an array of gpio_int_config containing the read-only configuration
 * for this interrupt.
 */
static const struct gpio_int_config gpio_int_data[] = {

	DT_FOREACH_CHILD(DT_IRQ_NODE, INT_CONFIG_FROM_NODE)
};
#endif

#undef INT_CONFIG_ENTRY
#undef INT_CONFIG_FROM_NODE

/*
 * Now initialize a pointer for each interrupt that points to the
 * configuration array entries. These are used externally
 * to reference the interrupts (to enable or disable).
 * These pointers are externally declared in gpio/gpio_int.h
 * and the names are referenced via a macro using the node label or
 * node id.
 */

#define INT_CONFIG_PTR_DECLARE(id)                                   \
	const struct gpio_int_config *const GPIO_INT_FROM_NODE(id) = \
		&gpio_int_data[GPIO_INT_ENUM(id)];

#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_gpio_interrupts)

DT_FOREACH_CHILD(DT_IRQ_NODE, INT_CONFIG_PTR_DECLARE)

#endif

#undef INT_CONFIG_PTR_DECLARE

#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_gpio_interrupts)
/*
 * Callback handler.
 * Call the stored interrupt handler.
 */
static void gpio_cb_handler(const struct device *dev,
			    struct gpio_callback *cbdata, uint32_t pins)
{
	/*
	 * Retrieve the array index from the callback pointer, and
	 * use that to get the interrupt config array entry.
	 */
	const struct gpio_int_config *conf =
		&gpio_int_data[cbdata - &int_cb_data[0]];
	conf->handler(conf->signal);
}

/*
 * Enable the interrupt.
 * Check whether the callback is already installed, and if
 * not, init and add the callback before enabling the
 * interrupt.
 */
int gpio_enable_dt_interrupt(const struct gpio_int_config *conf)
{
	/*
	 * Get the callback data associated with this interrupt
	 * by calculating the index in the gpio_int_config array.
	 */
	struct gpio_callback *cb = &int_cb_data[conf - &gpio_int_data[0]];
	gpio_flags_t flags;
	/*
	 * Check whether callback has been initialised.
	 */
	if (!cb->handler) {
		/*
		 * Initialise and add the callback.
		 */
		gpio_init_callback(cb, gpio_cb_handler, BIT(conf->pin));
		gpio_add_callback(conf->port, cb);
	}
	flags = (conf->flags | GPIO_INT_ENABLE) & ~GPIO_INT_DISABLE;
	return gpio_pin_interrupt_configure(conf->port, conf->pin, flags);
}

const struct gpio_int_config *
	gpio_interrupt_get_config(enum gpio_interrupts intr)
{
	return &gpio_int_data[intr];
}

#endif

/*
 * Disable the interrupt by setting the GPIO_INT_DISABLE flag.
 */
int gpio_disable_dt_interrupt(const struct gpio_int_config *conf)
{
	return gpio_pin_interrupt_configure(conf->port, conf->pin,
					    GPIO_INT_DISABLE);
}

/*
 * Mapping of GPIO signal to interrupt configuration block.
 */
static const struct gpio_int_config *
signal_to_interrupt(enum gpio_signal signal)
{
#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_gpio_interrupts)
	for (int i = 0; i < ARRAY_SIZE(gpio_int_data); i++) {
		if (signal == gpio_int_data[i].signal)
			return &gpio_int_data[i];
	}
#endif
	return NULL;
}

/*
 * Legacy API calls to enable/disable interrupts.
 */
int gpio_enable_interrupt(enum gpio_signal signal)
{
	const struct gpio_int_config *ic = signal_to_interrupt(signal);

	if (ic == NULL)
		return -1;

	return gpio_enable_dt_interrupt(ic);
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	const struct gpio_int_config *ic = signal_to_interrupt(signal);

	if (ic == NULL)
		return -1;

	return gpio_disable_dt_interrupt(ic);
}
