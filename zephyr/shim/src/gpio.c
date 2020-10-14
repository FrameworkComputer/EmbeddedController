/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <gpio.h>
#include <init.h>
#include <kernel.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(gpio_shim, LOG_LEVEL_ERR);

/*
 * Static information about each GPIO that is configured in the named_gpios
 * device tree node.
 */
struct gpio_config {
	const char *name;	 /* GPIO net name */
	const char *dev_name;	 /* Set at build time for lookup */
	gpio_pin_t pin;		 /* Bit number of pin within device */
	gpio_flags_t init_flags; /* From build time */
};

#define GPIO_CONFIG(id)                                      \
	{                                                    \
		.name = DT_LABEL(id),                        \
		.dev_name = DT_LABEL(DT_PHANDLE(id, gpios)), \
		.pin = DT_GPIO_PIN(id, gpios),               \
		.init_flags = DT_GPIO_FLAGS(id, gpios),      \
	},
static const struct gpio_config configs[] = {
#if DT_NODE_EXISTS(DT_PATH(named_gpios))
	DT_FOREACH_CHILD(DT_PATH(named_gpios), GPIO_CONFIG)
#endif
};

/* Runtime information for each GPIO that is configured in named_gpios */
struct gpio_data {
	const struct device *dev; /* Set during in init function */
};

#define GPIO_DATA(id) { },
static struct gpio_data data[] = {
#if DT_NODE_EXISTS(DT_PATH(named_gpios))
	DT_FOREACH_CHILD(DT_PATH(named_gpios), GPIO_DATA)
#endif
};


int gpio_is_implemented(enum gpio_signal signal)
{
	/* All GPIOs listed in Device Tree are consider implemented */
	return 1;
}

int gpio_get_level(enum gpio_signal signal)
{
	const int l = gpio_pin_get_raw(data[signal].dev, configs[signal].pin);

	if (l < 0) {
		LOG_ERR("Cannot read %s (%d)", configs[signal].name, l);
		return 0;
	}
	return l;
}

const char *gpio_get_name(enum gpio_signal signal)
{
	return configs[signal].name;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	int rv;

	if (value != 0) {
		rv = gpio_port_set_bits_raw(data[signal].dev,
					    BIT(configs[signal].pin));
	} else {
		rv = gpio_port_clear_bits_raw(data[signal].dev,
					      BIT(configs[signal].pin));
	}

	if (rv < 0) {
		LOG_ERR("Cannot write %s (%d)", configs[signal].name, rv);
	}
}

static int convert_from_zephyr_flags(const gpio_flags_t zephyr)
{
	int ec_flags = 0;

	/*
	 * Convert from Zephyr flags to EC flags. Note that a few flags have
	 * the same value in both builds environments (e.g. GPIO_OUTPUT)
	 */
	if (zephyr | GPIO_OUTPUT) {
		ec_flags |= GPIO_OUTPUT;
	}

	return ec_flags;
}

int gpio_get_default_flags(enum gpio_signal signal)
{
	return convert_from_zephyr_flags(configs[signal].init_flags);
}

static int init_gpios(const struct device *unused)
{
	ARG_UNUSED(unused);

	for (size_t i = 0; i < ARRAY_SIZE(configs); ++i) {
		data[i].dev = device_get_binding(configs[i].dev_name);

		if (data[i].dev == NULL) {
			LOG_ERR("Not found (%s)", configs[i].name);
		}

		const int rv = gpio_pin_configure(data[i].dev, configs[i].pin,
						  configs[i].init_flags);

		if (rv < 0) {
			LOG_ERR("Config failed %s (%d)", configs[i].name, rv);
		}
	}
	return 0;
}
SYS_INIT(init_gpios, PRE_KERNEL_1, 50);
