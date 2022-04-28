/* Copyright 2020 The Chromium OS Authors. All rights reserved.
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
#include "ioexpander.h"
#include "system.h"
#include "cros_version.h"

LOG_MODULE_REGISTER(gpio_shim, LOG_LEVEL_ERR);

/*
 * Static information about each GPIO that is configured in the named_gpios
 * device tree node.
 */
struct gpio_config {
	/* Access structure for lookup */
	struct gpio_dt_spec spec;
	/* GPIO net name */
	const char *name;
	/* From DTS, excludes interrupts flags */
	gpio_flags_t init_flags;
	/* From DTS, skips initialisation */
	bool no_auto_init;
};

/*
 * Initialise a gpio_dt_spec structure.
 * Normally the standard macro (GPIO_DT_SPEC_GET) could be used, but
 * the flags stored in our device tree config are the full 32 bit flags,
 * whereas the standard macros assume that only 8 bits of initial flags
 * will be needed.
 */
#define OUR_DT_SPEC(id)						\
	{							\
		.port = DEVICE_DT_GET(DT_GPIO_CTLR(id, gpios)),	\
		.pin = DT_GPIO_PIN(id, gpios),			\
		.dt_flags = 0xFF & (DT_GPIO_FLAGS(id, gpios)),	\
	}

#define GPIO_CONFIG(id)                                      \
	{                                                    \
		.spec = OUR_DT_SPEC(id),		     \
		.name = DT_NODE_FULL_NAME(id),               \
		.init_flags = DT_GPIO_FLAGS(id, gpios),      \
		.no_auto_init = DT_PROP(id, no_auto_init),   \
	},
static const struct gpio_config configs[] = {
#if DT_NODE_EXISTS(DT_PATH(named_gpios))
	DT_FOREACH_CHILD(DT_PATH(named_gpios), GPIO_CONFIG)
#endif
};

#undef GPIO_CONFIG
#undef OUR_DT_SPEC

/*
 * Generate a pointer for each GPIO, pointing to the gpio_dt_spec entry
 * in the table. These are named after the GPIO generated signal name,
 * so they can be used directly in Zephyr GPIO API calls.
 *
 * Potentially, instead of generating a pointer, the macro could
 * point directly into the table by exposing the gpio_config struct.
 */

#define GPIO_PTRS(id) const struct gpio_dt_spec * const	\
	GPIO_DT_NAME(GPIO_SIGNAL(id)) =			\
	&configs[GPIO_SIGNAL(id)].spec;

#if DT_NODE_EXISTS(DT_PATH(named_gpios))
DT_FOREACH_CHILD(DT_PATH(named_gpios), GPIO_PTRS)
#endif

int gpio_is_implemented(enum gpio_signal signal)
{
	return signal >= 0 && signal < ARRAY_SIZE(configs);
}

int gpio_get_level(enum gpio_signal signal)
{
	if (!gpio_is_implemented(signal))
		return 0;

	const int l = gpio_pin_get_raw(configs[signal].spec.port,
				       configs[signal].spec.pin);

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

	int rv = gpio_pin_set_raw(configs[signal].spec.port,
				  configs[signal].spec.pin,
				  value);

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

void gpio_or_ioex_set_level(int signal, int value)
{
	if (IS_ENABLED(CONFIG_PLATFORM_EC_IOEX) && signal_is_ioex(signal))
		ioex_set_level(signal, value);
	else
		gpio_set_level(signal, value);
}

int gpio_or_ioex_get_level(int signal, int *value)
{
	if (IS_ENABLED(CONFIG_PLATFORM_EC_IOEX) && signal_is_ioex(signal))
		return ioex_get_level(signal, value);
	*value = gpio_get_level(signal);
	return EC_SUCCESS;
}

/* GPIO flags which are the same in Zephyr and this codebase */
#define GPIO_CONVERSION_SAME_BITS                                             \
	(GPIO_OPEN_DRAIN | GPIO_PULL_UP | GPIO_PULL_DOWN | GPIO_VOLTAGE_1P8 | \
	 GPIO_INPUT | GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW |                    \
	GPIO_OUTPUT_INIT_HIGH)

#define FLAGS_HANDLED_FROM_ZEPHYR                                      \
	(GPIO_CONVERSION_SAME_BITS | GPIO_INT_ENABLE | GPIO_INT_EDGE | \
	GPIO_INT_HIGH_1 | GPIO_INT_LOW_0)

#define FLAGS_HANDLED_TO_ZEPHYR                                               \
	(GPIO_CONVERSION_SAME_BITS | GPIO_INT_F_RISING | GPIO_INT_F_FALLING | \
	GPIO_INT_F_LOW | GPIO_INT_F_HIGH)

int convert_from_zephyr_flags(const gpio_flags_t zephyr)
{
	/* Start out with the bits that are the same. */
	int ec_flags = zephyr & GPIO_CONVERSION_SAME_BITS;
	gpio_flags_t unhandled_flags = zephyr & (~FLAGS_HANDLED_FROM_ZEPHYR);

	/* TODO(b/173789980): handle conversion of more bits? */
	if (unhandled_flags) {
		LOG_WRN("Unhandled GPIO bits in zephyr->ec conversion: 0x%08X",
			unhandled_flags);
	}

	if (zephyr & GPIO_INT_ENABLE) {
		if (zephyr & GPIO_INT_EDGE) {
			if (zephyr & GPIO_INT_HIGH_1)
				ec_flags |= GPIO_INT_F_RISING;
			if (zephyr & GPIO_INT_LOW_0)
				ec_flags |= GPIO_INT_F_FALLING;
		} else {
			if (zephyr & GPIO_INT_LOW_0)
				ec_flags |= GPIO_INT_F_LOW;
			if (zephyr & GPIO_INT_HIGH_1)
				ec_flags |= GPIO_INT_F_HIGH;
		}
	}

	return ec_flags;
}

gpio_flags_t convert_to_zephyr_flags(int ec_flags)
{
	/* Start out with the bits that are the same. */
	gpio_flags_t zephyr_flags = ec_flags & GPIO_CONVERSION_SAME_BITS;
	int unhandled_flags = ec_flags & (~FLAGS_HANDLED_TO_ZEPHYR);

	/* TODO(b/173789980): handle conversion of more bits? */
	if (unhandled_flags) {
		LOG_WRN("Unhandled GPIO bits in ec->zephyr conversion: 0x%08X",
			unhandled_flags);
	}

	if (ec_flags & GPIO_INT_F_RISING)
		zephyr_flags |= GPIO_INT_ENABLE
			| GPIO_INT_EDGE | GPIO_INT_HIGH_1;
	if (ec_flags & GPIO_INT_F_FALLING)
		zephyr_flags |= GPIO_INT_ENABLE
			| GPIO_INT_EDGE | GPIO_INT_LOW_0;
	if (ec_flags & GPIO_INT_F_LOW)
		zephyr_flags |= GPIO_INT_ENABLE | GPIO_INT_LOW_0;
	if (ec_flags & GPIO_INT_F_HIGH)
		zephyr_flags |= GPIO_INT_ENABLE | GPIO_INT_HIGH_1;

	return zephyr_flags;
}

int gpio_get_default_flags(enum gpio_signal signal)
{
	if (!gpio_is_implemented(signal))
		return 0;

	return convert_from_zephyr_flags(configs[signal].init_flags);
}

const struct gpio_dt_spec *gpio_get_dt_spec(enum gpio_signal signal)
{
	if (!gpio_is_implemented(signal))
		return 0;
	return &configs[signal].spec;
}

static int init_gpios(const struct device *unused)
{
	gpio_flags_t flags;
	bool is_sys_jumped = system_jumped_to_this_image();

	ARG_UNUSED(unused);

	for (size_t i = 0; i < ARRAY_SIZE(configs); ++i) {
		int rv;

		/* Skip GPIOs that have set no-auto-init. */
		if (configs[i].no_auto_init)
			continue;

		if (!device_is_ready(configs[i].spec.port))
			LOG_ERR("Not found (%s)", configs[i].name);

		/*
		 * The configs[i].init_flags variable is read-only, so the
		 * following assignment is needed because the flags need
		 * adjusting on a warm reboot.
		 */
		flags = configs[i].init_flags;

		/*
		 * For warm boot, do not set the output state.
		 */
		if (is_sys_jumped && (flags & GPIO_OUTPUT)) {
			flags &=
			    ~(GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH);
		}

		rv = gpio_pin_configure_dt(&configs[i].spec, flags);
		if (rv < 0) {
			LOG_ERR("Config failed %s (%d)", configs[i].name, rv);
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

void gpio_reset(enum gpio_signal signal)
{
	if (!gpio_is_implemented(signal))
		return;

	gpio_pin_configure_dt(&configs[signal].spec,
			      configs[signal].init_flags);
}

void gpio_set_flags(enum gpio_signal signal, int flags)
{
	if (!gpio_is_implemented(signal))
		return;

	gpio_pin_configure_dt(&configs[signal].spec,
			      convert_to_zephyr_flags(flags));
}

int signal_is_gpio(int signal)
{
	return true;
}
