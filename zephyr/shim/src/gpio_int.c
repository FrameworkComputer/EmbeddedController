/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <init.h>
#include <kernel.h>
#include <logging/log.h>

#include "gpio.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "cros_version.h"

LOG_MODULE_REGISTER(gpio_int, LOG_LEVEL_ERR);

/*
 * Structure containing the callback block for a GPIO interrupt,
 * as well as the initial flags and the handler vector.
 * Everything except the callback data is const, so potentially
 * if space were at a premium, this structure could be split
 * into a RO and RW portion.
 */
struct gpio_int_config {
	struct gpio_callback cb; /* Callback data */
	void (*handler)(enum gpio_signal); /* Handler to call */
	enum gpio_signal arg; /* Argument for handler */
	gpio_flags_t flags; /* Flags */
	const struct device *port; /* GPIO device */
	gpio_pin_t pin; /* GPIO pin */
};

#define DT_IRQ_NODE	DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_gpio_interrupts)
/*
 * Create an instance of a gpio_int_config from a DTS node
 */

#define GPIO_INT_FUNC(name) extern void name(enum gpio_signal)

#define GPIO_INT_CREATE(id, irq_pin)					\
	GPIO_INT_FUNC(DT_STRING_TOKEN(id, handler));			\
	struct gpio_int_config GPIO_INT_FROM_NODE(id) = {		\
		.handler = DT_STRING_TOKEN(id, handler),		\
		.arg = GPIO_SIGNAL(irq_pin),				\
		.flags = DT_PROP(id, flags),				\
		.port = DEVICE_DT_GET(DT_GPIO_CTLR(irq_pin, gpios)),	\
		.pin = DT_GPIO_PIN(irq_pin, gpios),			\
	};

#define GPIO_INT_DEFN(id) GPIO_INT_CREATE(id, DT_PHANDLE(id, irq_pin))

#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_gpio_interrupts)
	DT_FOREACH_CHILD(DT_IRQ_NODE, GPIO_INT_DEFN)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(cros_ec_gpio_interrupts) == 1,
	"Only one node for cros_ec_gpio_interrupts is allowed");
#endif

#undef GPIO_INT_FUNC
#undef GPIO_INT_CREATE
#undef GPIO_INT_DEFN

/*
 * Callback handler.
 * Call the stored interrupt handler.
 */
static void gpio_cb_handler(const struct device *dev,
			    struct gpio_callback *cbdata,
			    uint32_t pins)
{
	struct gpio_int_config *conf =
		CONTAINER_OF(cbdata, struct gpio_int_config, cb);
	conf->handler(conf->arg);
}

/*
 * Enable the interrupt.
 * Check whether the callback is already installed, and if
 * not, init and add the callback before enabling the
 * interrupt.
 */
int gpio_enable_dt_interrupt(struct gpio_int_config *conf)
{
	gpio_flags_t flags;
	/*
	 * Check whether callback has been initialised.
	 */
	if (!conf->cb.handler) {
		/*
		 * Initialise and add the callback.
		 */
		gpio_init_callback(&conf->cb, gpio_cb_handler, BIT(conf->pin));
		gpio_add_callback(conf->port, &conf->cb);
	}
	flags = (conf->flags | GPIO_INT_ENABLE) & ~GPIO_INT_DISABLE;
	return gpio_pin_interrupt_configure(conf->port, conf->pin, flags);
}

/*
 * Disable the interrupt by setting the GPIO_INT_DISABLE flag.
 */
int gpio_disable_dt_interrupt(struct gpio_int_config *conf)
{
	return gpio_pin_interrupt_configure(conf->port,
					    conf->pin,
					    GPIO_INT_DISABLE);
}

/*
 * Create a mapping table from an enum gpio_signal to
 * a struct gpio_int_config so that legacy code can use
 * the gpio_signal to enable/disable interrupts
 */

#define GPIO_SIG_MAP_ENTRY(id, irq_pin)                                       \
	COND_CODE_1(DT_NODE_HAS_PROP(irq_pin, enum_name),                 \
		    (                                                      \
			    {                                              \
				    .signal = DT_STRING_UPPER_TOKEN(       \
					    irq_pin, enum_name),          \
				    .config = &GPIO_INT_FROM_NODE(id), \
			    },),                                           \
		    ())

#define GPIO_SIG_MAP(id) GPIO_SIG_MAP_ENTRY(id, DT_PHANDLE(id, irq_pin))

static const struct {
	enum gpio_signal signal;
	struct gpio_int_config *config;
} signal_to_int[] = {
#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_gpio_interrupts)
	DT_FOREACH_CHILD(DT_IRQ_NODE, GPIO_SIG_MAP)
#endif
};

#undef GPIO_SIG_MAP
#undef GPIO_SIG_MAP_ENTRY

/*
 * Mapping of GPIO signal to interrupt configuration block.
 */
static struct gpio_int_config *signal_to_interrupt(enum gpio_signal signal)
{
	for (int i = 0; i < ARRAY_SIZE(signal_to_int); i++) {
		if (signal == signal_to_int[i].signal)
			return signal_to_int[i].config;
	}
	return NULL;
}

/*
 * Legacy API calls to enable/disable interrupts.
 */
int gpio_enable_interrupt(enum gpio_signal signal)
{
	struct gpio_int_config *ic = signal_to_interrupt(signal);

	if (ic == NULL)
		return -1;

	return gpio_enable_dt_interrupt(ic);
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	struct gpio_int_config *ic = signal_to_interrupt(signal);

	if (ic == NULL)
		return -1;

	return gpio_disable_dt_interrupt(ic);
}
