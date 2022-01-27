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
 * TODO(b:214608987): Once all interrupts have been transitioned to
 * the new API, the legacy interrupt handling can be removed.
 */

/* Maps platform/ec gpio callback information */
struct gpio_signal_callback {
	/* The platform/ec gpio_signal */
	const enum gpio_signal signal;
	/* IRQ handler from platform/ec code */
	void (*const irq_handler)(enum gpio_signal signal);
	/* Interrupt-related gpio flags */
	const gpio_flags_t flags;
};

/*
 * EC_CROS_GPIO_INTERRUPTS is now deprecated, and new boards should
 * use the "cros-ec,gpio-interrupts" bindings instead.
 * This will be removed once all boards are converted.
 *
 * Each zephyr project should define EC_CROS_GPIO_INTERRUPTS in their gpio_map.h
 * file if there are any interrupts that should be registered.  The
 * corresponding handler will be declared here, which will prevent
 * needing to include headers with complex dependencies in gpio_map.h.
 *
 * EC_CROS_GPIO_INTERRUPTS is a space-separated list of GPIO_INT items.
 */

#if (defined(EC_CROS_GPIO_INTERRUPTS) &&  \
	DT_HAS_COMPAT_STATUS_OKAY(cros_ec_gpio_interrupts))
#error "Cannot use both EC_CROS_GPIO_INTERRUPTS and cros_ec_gpio_interrupts"
#endif

/*
 * Validate interrupt flags are valid for the Zephyr GPIO driver.
 */
#define GPIO_INT(sig, f, cb)                       \
	BUILD_ASSERT(VALID_GPIO_INTERRUPT_FLAG(f), \
		     STRINGIFY(sig) " is not using Zephyr interrupt flags");
#ifdef EC_CROS_GPIO_INTERRUPTS
	EC_CROS_GPIO_INTERRUPTS
#endif
#undef GPIO_INT

/*
 * Create unique enum values for each GPIO_INT entry, which also sets
 * the ZEPHYR_GPIO_INT_COUNT value.
 */
#define ZEPHYR_GPIO_INT_ID(sig) INT_##sig
#define GPIO_INT(sig, f, cb) ZEPHYR_GPIO_INT_ID(sig),
enum zephyr_gpio_int_id {
#ifdef EC_CROS_GPIO_INTERRUPTS
	EC_CROS_GPIO_INTERRUPTS
#endif
	ZEPHYR_GPIO_INT_COUNT,
};
#undef GPIO_INT

/* Create prototypes for each GPIO IRQ handler */
#define GPIO_INT(sig, f, cb) void cb(enum gpio_signal signal);
#ifdef EC_CROS_GPIO_INTERRUPTS
EC_CROS_GPIO_INTERRUPTS
#endif
#undef GPIO_INT

/*
 * The Zephyr gpio_callback data needs to be updated at runtime, so allocate
 * into uninitialized data (BSS). The constant data pulled from
 * EC_CROS_GPIO_INTERRUPTS is stored separately in the gpio_interrupts[] array.
 */
static struct gpio_callback zephyr_gpio_callbacks[ZEPHYR_GPIO_INT_COUNT];

#define ZEPHYR_GPIO_CALLBACK_TO_INDEX(cb)                 \
	(int)(((int)(cb) - (int)&zephyr_gpio_callbacks) / \
	      sizeof(struct gpio_callback))

#define GPIO_INT(sig, f, cb)       \
	{                          \
		.signal = sig,     \
		.flags = f,        \
		.irq_handler = cb, \
	},
const static struct gpio_signal_callback
	gpio_interrupts[ZEPHYR_GPIO_INT_COUNT] = {
#ifdef EC_CROS_GPIO_INTERRUPTS
	EC_CROS_GPIO_INTERRUPTS
#endif
#undef GPIO_INT
	};

/* The single zephyr gpio handler that routes to appropriate platform/ec cb */
static void gpio_handler_shim(const struct device *port,
			      struct gpio_callback *cb, gpio_port_pins_t pins)
{
	int callback_index = ZEPHYR_GPIO_CALLBACK_TO_INDEX(cb);
	const struct gpio_signal_callback *const gpio =
		&gpio_interrupts[callback_index];

	/* Call the platform/ec gpio interrupt handler */
	gpio->irq_handler(gpio->signal);
}

/**
 * get_interrupt_from_signal() - Translate a gpio_signal to the
 * corresponding gpio_signal_callback
 *
 * @signal		The signal to convert.
 *
 * Return: A pointer to the corresponding entry in gpio_interrupts, or
 * NULL if one does not exist.
 */
const static struct gpio_signal_callback *
get_interrupt_from_signal(enum gpio_signal signal)
{
	if (!gpio_is_implemented(signal))
		return NULL;

	for (size_t i = 0; i < ARRAY_SIZE(gpio_interrupts); i++) {
		if (gpio_interrupts[i].signal == signal)
			return &gpio_interrupts[i];
	}

	LOG_ERR("No interrupt defined for GPIO %s", gpio_get_name(signal));
	return NULL;
}

/*
 * Zephyr based interrupt handling.
 */

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

#define GPIO_INT_CREATE(id, irq_gpio)					\
	GPIO_INT_FUNC(DT_STRING_TOKEN(id, handler));			\
	struct gpio_int_config GPIO_INT_FROM_NODE(id) = {		\
		.handler = DT_STRING_TOKEN(id, handler),		\
		.arg = GPIO_SIGNAL(irq_gpio),				\
		.flags = DT_PROP(id, flags),				\
		.port = DEVICE_DT_GET(DT_GPIO_CTLR(irq_gpio, gpios)),	\
		.pin = DT_GPIO_PIN(irq_gpio, gpios),			\
	};

#define GPIO_INT_DEFN(id) GPIO_INT_CREATE(id, DT_PROP(id, irq_gpio))

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

#define GPIO_SIG_MAP_ENTRY(id, irq_gpio)                                       \
	COND_CODE_1(DT_NODE_HAS_PROP(irq_gpio, enum_name),                 \
		    (                                                      \
			    {                                              \
				    .signal = DT_STRING_UPPER_TOKEN(       \
					    irq_gpio, enum_name),          \
				    .config = &GPIO_INT_FROM_NODE(id),	   \
			    },),                                           \
		    ())

#define GPIO_SIG_MAP(id) GPIO_SIG_MAP_ENTRY(id, DT_PROP(id, irq_gpio))

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

int gpio_enable_interrupt(enum gpio_signal signal)
{
	int rv;
	const struct gpio_signal_callback *interrupt;

	struct gpio_int_config *ic;

	ic = signal_to_interrupt(signal);
	if (ic) {
		return gpio_enable_dt_interrupt(ic);
	}
	interrupt = get_interrupt_from_signal(signal);

	if (!interrupt)
		return -1;

	/*
	 * Config interrupt flags (e.g. INT_EDGE_BOTH) & enable interrupt
	 * together.
	 */
	rv = gpio_pin_interrupt_configure_dt(gpio_get_dt_spec(signal),
					     (interrupt->flags |
					      GPIO_INT_ENABLE) &
					      ~GPIO_INT_DISABLE);
	if (rv < 0) {
		LOG_ERR("Failed to enable interrupt on %s (%d)",
			gpio_get_name(signal), rv);
	}

	return rv;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	int rv;

	struct gpio_int_config *ic;

	ic = signal_to_interrupt(signal);
	if (ic) {
		return gpio_disable_dt_interrupt(ic);
	}
	if (!gpio_is_implemented(signal))
		return -1;

	rv = gpio_pin_interrupt_configure_dt(gpio_get_dt_spec(signal),
					     GPIO_INT_DISABLE);
	if (rv < 0) {
		LOG_ERR("Failed to disable interrupt on %s (%d)",
			gpio_get_name(signal), rv);
	}

	return rv;
}

static int init_gpio_ints(const struct device *unused)
{
	ARG_UNUSED(unused);

	/*
	 * Loop through all interrupt pins and set their callback.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(gpio_interrupts); ++i) {
		const enum gpio_signal signal = gpio_interrupts[i].signal;
		int rv;

		if (signal == GPIO_UNIMPLEMENTED)
			continue;

		const struct gpio_dt_spec *spec = gpio_get_dt_spec(signal);
		gpio_init_callback(&zephyr_gpio_callbacks[i], gpio_handler_shim,
				   BIT(spec->pin));
		rv = gpio_add_callback(spec->port, &zephyr_gpio_callbacks[i]);

		if (rv < 0) {
			LOG_ERR("Callback reg failed %s (%d)",
				gpio_get_name(signal), rv);
			continue;
		}
	}
	return 0;
}
#if CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY <= CONFIG_KERNEL_INIT_PRIORITY_DEFAULT
#error "GPIO interrupts must initialize after the kernel default initialization"
#endif
SYS_INIT(init_gpio_ints, POST_KERNEL, CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY);
