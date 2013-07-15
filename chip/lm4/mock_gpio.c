/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock GPIO module for Chrome EC */

#include "console.h"
#include "gpio.h"
#include "util.h"


static int8_t mock_value[GPIO_COUNT] = {0};
static int8_t mock_gpio_im[GPIO_COUNT] = {0};


int gpio_pre_init(void)
{
	/* Nothing to do */
	return EC_SUCCESS;
}


void gpio_set_alternate_function(int port, int mask, int func)
{
	/* Not implemented */
	return;
}


const char *gpio_get_name(enum gpio_signal signal)
{
	return gpio_list[signal].name;
}


int gpio_get_level(enum gpio_signal signal)
{
	return mock_value[signal] ? 1 : 0;
}


int gpio_set_level(enum gpio_signal signal, int value)
{
	mock_value[signal] = value;
	return EC_SUCCESS;
}


int gpio_set_flags(enum gpio_signal signal, int flags)
{
	/* Not implemented */
	return EC_SUCCESS;
}


int gpio_enable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	/* Fail if no interrupt handler */
	if (!g->irq_handler)
		return EC_ERROR_UNKNOWN;

	mock_gpio_im[signal] = 1;
	return EC_SUCCESS;
}


/* Find a GPIO signal by name.  Returns the signal index, or GPIO_COUNT if
 * no match. */
static enum gpio_signal find_signal_by_name(const char *name)
{
	const struct gpio_info *g = gpio_list;
	int i;

	if (!name || !*name)
		return GPIO_COUNT;

	for (i = 0; i < GPIO_COUNT; i++, g++) {
		if (!strcasecmp(name, g->name))
			return i;
	}

	return GPIO_COUNT;
}


static int command_gpio_mock(int argc, char **argv)
{
	char *e;
	int v, i;
	const struct gpio_info *g;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	i = find_signal_by_name(argv[1]);
	if (i == GPIO_COUNT)
		return EC_ERROR_PARAM1;
	g = gpio_list + i;

	v = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	gpio_set_level(i, v);

	if (g->irq_handler && mock_gpio_im[i])
		g->irq_handler(i);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(gpiomock, command_gpio_mock,
			"name <0 | 1>",
			"Mock a GPIO input",
			NULL);
