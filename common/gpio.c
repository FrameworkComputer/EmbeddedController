/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO common functionality for Chrome EC */

#include "common.h"
#include "gpio.h"
#include "ioexpander.h"
#include "registers.h"
#include "timer.h"
#include "util.h"

/* GPIO alternate function structure */
struct gpio_alt_func {
	/* Module ID (as uint8_t, since enum would be 32-bit) */
	uint8_t module_id;

	/* Alternate function number */
	enum gpio_alternate_func func;

	/* Port base address */
	uint32_t port;

	/* Bitmask on that port (multiple bits allowed) */
	uint32_t mask;

	/* Flags (GPIO_*; see above). */
	uint32_t flags;
};

/*
 * Construct the gpio_alt_funcs array.  This array is used by gpio_config_module
 * to enable and disable GPIO alternate functions on a module by module basis.
 */
#define ALTERNATE(pinmask, function, module, flagz)                            \
	{GPIO_##pinmask, .func = (function), .module_id = (module),            \
	 .flags = (flagz)},

static const struct gpio_alt_func gpio_alt_funcs[] = {
#include "gpio.wrap"
};

/*
 * GPIO_CONFIG_ALL_PORTS signifies a "don't care" for the GPIO port.  This is
 * used in gpio_config_pins().  When the port parameter is set to this, the
 * pin_mask parameter is ignored.
 */
#define GPIO_CONFIG_ALL_PORTS 0xFFFFFFFF

static int gpio_config_pins(enum module_id id, uint32_t port, uint32_t pin_mask,
			    int enable)
{
	const struct gpio_alt_func *af;
	int rv = EC_ERROR_INVAL;

	/* Find pins and set to alternate functions */
	for (af = gpio_alt_funcs;
	     af < gpio_alt_funcs + ARRAY_SIZE(gpio_alt_funcs); af++) {
		if (af->module_id != id)
			continue; /* Pins for some other module */

		/* Check to see if the requested port matches. */
		if ((port != GPIO_CONFIG_ALL_PORTS) && (port != af->port))
			continue;

		/* If we don't care which port, enable all applicable pins. */
		if (port == GPIO_CONFIG_ALL_PORTS)
			pin_mask = af->mask;

		if ((af->mask & pin_mask) == pin_mask) {
			if (!(af->flags & GPIO_DEFAULT))
				gpio_set_flags_by_mask(
					af->port, (af->mask & pin_mask),
					enable ? af->flags : GPIO_INPUT);
			gpio_set_alternate_function(af->port,
				    (af->mask & pin_mask),
				    enable ? af->func : GPIO_ALT_FUNC_NONE);
			rv = EC_SUCCESS;
			/* We're done here if we were just setting one port. */
			if (port != GPIO_CONFIG_ALL_PORTS)
				break;
		}
	}

	return rv;
}

/*****************************************************************************/
/* GPIO API */

int gpio_config_module(enum module_id id, int enable)
{
	/* Set all the alternate functions for this module. */
	return gpio_config_pins(id, GPIO_CONFIG_ALL_PORTS, 0, enable);
}

int gpio_config_pin(enum module_id id, enum gpio_signal signal, int enable)
{
	return gpio_config_pins(id, gpio_list[signal].port,
				gpio_list[signal].mask, enable);
}

void gpio_set_flags(enum gpio_signal signal, int flags)
{
	const struct gpio_info *g = gpio_list + signal;

	gpio_set_flags_by_mask(g->port, g->mask, flags);
}

#ifdef CONFIG_GPIO_GET_EXTENDED
int gpio_get_flags(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	return gpio_get_flags_by_mask(g->port, g->mask);
}
#endif

int gpio_get_default_flags(enum gpio_signal signal)
{
	return gpio_list[signal].flags;
}

void gpio_reset(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	gpio_set_flags_by_mask(g->port, g->mask, g->flags);
	gpio_set_alternate_function(g->port, g->mask, GPIO_ALT_FUNC_NONE);
}

const char *gpio_get_name(enum gpio_signal signal)
{
	return gpio_list[signal].name;
}

int gpio_is_implemented(enum gpio_signal signal)
{
	return !!gpio_list[signal].mask;
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

#ifdef CONFIG_GPIO_POWER_DOWN
/*
 * Power down a group of GPIO pins marked with a module ID
 * in board/board_name/gpio.inc
 * Hibernation/sleep entry:
 * gpio_power_down_module(MODULE_xxxx)
 * Chip level code will power down all pins in module.
 * Wake:
 * Use gpio_config_module to put the module pin(s)
 * back to enabled state. Chip level code will re-power
 * and configure the pin(s).
 * This mechanism does not handle dynamic changing of
 * pin configuration at run time.
 */
int gpio_power_down_module(enum module_id id)
{
	const struct gpio_alt_func *af;
	int rv = EC_ERROR_INVAL;

	/* Find pins and power down */
	for (af = gpio_alt_funcs;
	     af < gpio_alt_funcs + ARRAY_SIZE(gpio_alt_funcs); af++) {
		if (af->module_id != id)
			continue; /* Pins for some other module */

		gpio_set_flags_by_mask(af->port, af->mask, GPIO_POWER_DOWN);
		rv = EC_SUCCESS;
	}

	return rv;
}
#endif /* #ifdef CONFIG_GPIO_POWER_DOWN */

void gpio_set_level_verbose(enum console_channel channel,
			    enum gpio_signal signal, int value)
{
	ASSERT(signal_is_gpio(signal));
	cprints(channel, "Set %s: %d", gpio_get_name(signal), value);
	gpio_set_level(signal, value);
}

void gpio_or_ioex_set_level(int signal, int value)
{
	if (IS_ENABLED(CONFIG_IO_EXPANDER) && signal_is_ioex(signal))
		ioex_set_level(signal, value);
	else
		gpio_set_level(signal, value);
}

int signal_is_gpio(int signal)
{
	return ((signal >= GPIO_SIGNAL_START)
		&& (signal < GPIO_SIGNAL_START + GPIO_COUNT));
}

/*****************************************************************************/
