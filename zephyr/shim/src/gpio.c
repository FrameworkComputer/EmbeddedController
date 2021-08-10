/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <init.h>
#include <kernel.h>
#include <logging/log.h>

#include "gpio.h"
#include "gpio/gpio.h"
#include "sysjump.h"
#include "cros_version.h"

LOG_MODULE_REGISTER(gpio_shim, LOG_LEVEL_ERR);

/*
 * Static information about each GPIO that is configured in the named_gpios
 * device tree node.
 */
struct gpio_config {
	/* GPIO net name */
	const char *name;
	/* Set at build time for lookup */
	const struct device *dev;
	/* Bit number of pin within device */
	gpio_pin_t pin;
	/* From DTS, excludes interrupts flags */
	gpio_flags_t init_flags;
};

#define GPIO_CONFIG(id)                                                      \
	COND_CODE_1(                                                         \
		DT_NODE_HAS_PROP(id, enum_name),                             \
		(                                                            \
			{                                                    \
				.name = DT_LABEL(id),                        \
				.dev = DEVICE_DT_GET(DT_PHANDLE(id, gpios)), \
				.pin = DT_GPIO_PIN(id, gpios),               \
				.init_flags = DT_GPIO_FLAGS(id, gpios),      \
			}, ),                                                \
		())
static const struct gpio_config configs[] = {
#if DT_NODE_EXISTS(DT_PATH(named_gpios))
	DT_FOREACH_CHILD(DT_PATH(named_gpios), GPIO_CONFIG)
#endif
};

/* Runtime information for each GPIO that is configured in named_gpios */
struct gpio_data {
	/* Runtime device for gpio port. Set during in init function */
	const struct device *dev;
};

#define GPIO_DATA(id) COND_CODE_1(DT_NODE_HAS_PROP(id, enum_name), ({}, ), ())
static struct gpio_data data[] = {
#if DT_NODE_EXISTS(DT_PATH(named_gpios))
	DT_FOREACH_CHILD(DT_PATH(named_gpios), GPIO_DATA)
#endif
};

/* Maps platform/ec gpio callbacks to zephyr gpio callbacks */
struct gpio_signal_callback {
	/* The platform/ec gpio_signal */
	const enum gpio_signal signal;
	/* Zephyr callback */
	struct gpio_callback callback;
	/* IRQ handler from platform/ec code */
	void (*const irq_handler)(enum gpio_signal signal);
	/* Interrupt-related gpio flags */
	const gpio_flags_t flags;
};

/* The single zephyr gpio handler that routes to appropriate platform/ec cb */
static void gpio_handler_shim(const struct device *port,
			      struct gpio_callback *cb, gpio_port_pins_t pins)
{
	const struct gpio_signal_callback *const gpio =
		CONTAINER_OF(cb, struct gpio_signal_callback, callback);

	/* Call the platform/ec gpio interrupt handler */
	gpio->irq_handler(gpio->signal);
}

/*
 * Validate interrupt flags are valid for the Zephyr GPIO driver.
 */
#define IS_GPIO_INTERRUPT_FLAG(flag, mask) ((flag & mask) == mask)
#define VALID_GPIO_INTERRUPT_FLAG(flag)                             \
	(IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_EDGE_RISING) ||      \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_EDGE_FALLING) ||     \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_EDGE_BOTH) ||        \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_LEVEL_LOW) ||        \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_LEVEL_HIGH) ||       \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_EDGE_TO_INACTIVE) || \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_EDGE_TO_ACTIVE) ||   \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_LEVEL_INACTIVE) ||   \
	 IS_GPIO_INTERRUPT_FLAG(flag, GPIO_INT_LEVEL_ACTIVE))

#define GPIO_INT(sig, f, cb)                       \
	BUILD_ASSERT(VALID_GPIO_INTERRUPT_FLAG(f), \
		     STRINGIFY(sig) " is not using Zephyr interrupt flags");
#ifdef EC_CROS_GPIO_INTERRUPTS
EC_CROS_GPIO_INTERRUPTS
#endif
#undef GPIO_INT

/*
 * Each zephyr project should define EC_CROS_GPIO_INTERRUPTS in their gpio_map.h
 * file if there are any interrupts that should be registered.  The
 * corresponding handler will be declared here, which will prevent
 * needing to include headers with complex dependencies in gpio_map.h.
 *
 * EC_CROS_GPIO_INTERRUPTS is a space-separated list of GPIO_INT items.
 */
#define GPIO_INT(sig, f, cb) void cb(enum gpio_signal signal);
#ifdef EC_CROS_GPIO_INTERRUPTS
EC_CROS_GPIO_INTERRUPTS
#endif
#undef GPIO_INT

#define GPIO_INT(sig, f, cb)       \
	{                          \
		.signal = sig,     \
		.flags = f,        \
		.irq_handler = cb, \
	},
struct gpio_signal_callback gpio_interrupts[] = {
#ifdef EC_CROS_GPIO_INTERRUPTS
	EC_CROS_GPIO_INTERRUPTS
#endif
#undef GPIO_INT
};

/**
 * get_interrupt_from_signal() - Translate a gpio_signal to the
 * corresponding gpio_signal_callback
 *
 * @signal		The signal to convert.
 *
 * Return: A pointer to the corresponding entry in gpio_interrupts, or
 * NULL if one does not exist.
 */
static struct gpio_signal_callback *
get_interrupt_from_signal(enum gpio_signal signal)
{
	if (!gpio_is_implemented(signal))
		return NULL;

	for (size_t i = 0; i < ARRAY_SIZE(gpio_interrupts); i++) {
		if (gpio_interrupts[i].signal == signal)
			return &gpio_interrupts[i];
	}

	LOG_ERR("No interrupt defined for GPIO %s", configs[signal].name);
	return NULL;
}

int gpio_is_implemented(enum gpio_signal signal)
{
	return signal >= 0 && signal < ARRAY_SIZE(configs);
}

int gpio_get_level(enum gpio_signal signal)
{
	if (!gpio_is_implemented(signal))
		return 0;

	const int l = gpio_pin_get_raw(data[signal].dev, configs[signal].pin);

	if (l < 0) {
		LOG_ERR("Cannot read %s (%d)", configs[signal].name, l);
		return 0;
	}
	return l;
}

int gpio_get_ternary(enum gpio_signal signal)
{
	int pd, pu;
	int flags = gpio_get_default_flags(signal);

	/* Read GPIO with internal pull-down */
	gpio_set_flags(signal, GPIO_INPUT | GPIO_PULL_DOWN);
	pd = gpio_get_level(signal);
	udelay(100);

	/* Read GPIO with internal pull-up */
	gpio_set_flags(signal, GPIO_INPUT | GPIO_PULL_UP);
	pu = gpio_get_level(signal);
	udelay(100);

	/* Reset GPIO flags */
	gpio_set_flags(signal, flags);

	/* Check PU and PD readings to determine tristate */
	return pu && !pd ? 2 : pd;
}

const char *gpio_get_name(enum gpio_signal signal)
{
	if (!gpio_is_implemented(signal))
		return "UNIMPLEMENTED";

	return configs[signal].name;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	if (!gpio_is_implemented(signal))
		return;

	int rv = gpio_pin_set_raw(data[signal].dev, configs[signal].pin, value);

	if (rv < 0) {
		LOG_ERR("Cannot write %s (%d)", configs[signal].name, rv);
	}
}

void gpio_set_level_verbose(enum console_channel channel,
			    enum gpio_signal signal, int value)
{
	cprints(channel, "Set %s: %d", gpio_get_name(signal), value);
	gpio_set_level(signal, value);
}

/* GPIO flags which are the same in Zephyr and this codebase */
#define GPIO_CONVERSION_SAME_BITS                                       \
	(GPIO_OPEN_DRAIN | GPIO_PULL_UP | GPIO_PULL_DOWN | GPIO_INPUT | \
	 GPIO_OUTPUT)

static int convert_from_zephyr_flags(const gpio_flags_t zephyr)
{
	/* Start out with the bits that are the same. */
	int ec_flags = zephyr & GPIO_CONVERSION_SAME_BITS;
	gpio_flags_t unhandled_flags = zephyr & ~GPIO_CONVERSION_SAME_BITS;

	/* TODO(b/173789980): handle conversion of more bits? */
	if (unhandled_flags) {
		LOG_WRN("Unhandled GPIO bits in zephyr->ec conversion: 0x%08X",
			unhandled_flags);
	}

	return ec_flags;
}

static gpio_flags_t convert_to_zephyr_flags(int ec_flags)
{
	/* Start out with the bits that are the same. */
	gpio_flags_t zephyr_flags = ec_flags & GPIO_CONVERSION_SAME_BITS;
	int unhandled_flags = ec_flags & ~GPIO_CONVERSION_SAME_BITS;

	/* TODO(b/173789980): handle conversion of more bits? */
	if (unhandled_flags) {
		LOG_WRN("Unhandled GPIO bits in ec->zephyr conversion: 0x%08X",
			unhandled_flags);
	}

	return zephyr_flags;
}

int gpio_get_default_flags(enum gpio_signal signal)
{
	if (!gpio_is_implemented(signal))
		return 0;

	return convert_from_zephyr_flags(configs[signal].init_flags);
}

static int init_gpios(const struct device *unused)
{
	gpio_flags_t flags;
	struct jump_data *jdata;
	bool is_sys_jumped;

	ARG_UNUSED(unused);

	jdata = get_jump_data();

	if (jdata && jdata->magic == JUMP_DATA_MAGIC)
		is_sys_jumped = true;
	else
		is_sys_jumped = false;

	/* Loop through all GPIOs in device tree to set initial configuration */
	for (size_t i = 0; i < ARRAY_SIZE(configs); ++i) {
		data[i].dev = configs[i].dev;
		int rv;

		if (!device_is_ready(data[i].dev))
			LOG_ERR("Not found (%s)", configs[i].name);

		/*
		 * The configs[i].init_flags variable is read-only, so the
		 * following assignment is needed because the flags need
		 * adjusting on a warm reboot.
		 */
		flags = configs[i].init_flags;

		if (is_sys_jumped) {
			flags &=
				~(GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH);
		}

		rv = gpio_pin_configure(data[i].dev, configs[i].pin, flags);
		if (rv < 0) {
			LOG_ERR("Config failed %s (%d)", configs[i].name, rv);
		}
	}

	/*
	 * Loop through all interrupt pins and set their callback.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(gpio_interrupts); ++i) {
		const enum gpio_signal signal = gpio_interrupts[i].signal;
		int rv;

		if (signal == GPIO_UNIMPLEMENTED)
			continue;

		gpio_init_callback(&gpio_interrupts[i].callback,
				   gpio_handler_shim, BIT(configs[signal].pin));
		rv = gpio_add_callback(data[signal].dev,
				       &gpio_interrupts[i].callback);

		if (rv < 0) {
			LOG_ERR("Callback reg failed %s (%d)",
				configs[signal].name, rv);
			continue;
		}
	}

	/* Configure unused pins in chip driver for better power consumption */
	if (gpio_config_unused_pins) {
		int rv;

		rv = gpio_config_unused_pins();
		if (rv < 0) {
			return rv;
		}
	}

	return 0;
}
#if CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY <= CONFIG_KERNEL_INIT_PRIORITY_DEFAULT
#error "GPIOs must initialize after the kernel default initialization"
#endif
SYS_INIT(init_gpios, POST_KERNEL, CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY);

int gpio_enable_interrupt(enum gpio_signal signal)
{
	int rv;
	struct gpio_signal_callback *interrupt;

	interrupt = get_interrupt_from_signal(signal);

	if (!interrupt)
		return -1;

	/*
	 * Config interrupt flags (e.g. INT_EDGE_BOTH) & enable interrupt
	 * together.
	 */
	rv = gpio_pin_interrupt_configure(data[signal].dev, configs[signal].pin,
					  (interrupt->flags | GPIO_INT_ENABLE) &
						  ~GPIO_INT_DISABLE);
	if (rv < 0) {
		LOG_ERR("Failed to enable interrupt on %s (%d)",
			configs[signal].name, rv);
	}

	return rv;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	int rv;

	if (!gpio_is_implemented(signal))
		return -1;

	rv = gpio_pin_interrupt_configure(data[signal].dev, configs[signal].pin,
					  GPIO_INT_DISABLE);
	if (rv < 0) {
		LOG_ERR("Failed to disable interrupt on %s (%d)",
			configs[signal].name, rv);
	}

	return rv;
}

void gpio_reset(enum gpio_signal signal)
{
	if (!gpio_is_implemented(signal))
		return;

	gpio_pin_configure(data[signal].dev, configs[signal].pin,
			   configs[signal].init_flags);
}

void gpio_set_flags(enum gpio_signal signal, int flags)
{
	if (!gpio_is_implemented(signal))
		return;

	gpio_pin_configure(data[signal].dev, configs[signal].pin,
			   convert_to_zephyr_flags(flags));
}

int signal_is_gpio(int signal)
{
	return true;
}
