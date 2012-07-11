/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO console commands for Chrome EC */

#include "board.h"
#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "system.h"
#include "util.h"


static uint8_t last_val[(GPIO_COUNT + 7) / 8];


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


/* If v is different from the last value for index i, updates the last value
 * and returns 1; else returns 0. */
static int last_val_changed(int i, int v)
{
	if (v && !(last_val[i / 8] & (1 << (i % 8)))) {
		last_val[i / 8] |= 1 << (i % 8);
		return 1;
	} else if (!v && last_val[i / 8] & (1 << (i % 8))) {
		last_val[i / 8] &= ~(1 << (i % 8));
		return 1;
	} else {
		return 0;
	}
}

/*****************************************************************************/
/* Console commands */

static int command_gpio_get(int argc, char **argv)
{
	const struct gpio_info *g = gpio_list;
	int changed, v, i;

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		i = find_signal_by_name(argv[1]);
		if (i == GPIO_COUNT)
			return EC_ERROR_PARAM1;
		g = gpio_list + i;
		v = gpio_get_level(i);
		changed = last_val_changed(i, v);
		ccprintf("  %d%c %s\n", v, (changed ? '*' : ' '), g->name);

		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		if (!g->mask)
			continue;  /* Skip unsupported signals */

		v = gpio_get_level(i);
		changed = last_val_changed(i, v);
		ccprintf("  %d%c %s\n", v, (changed ? '*' : ' '), g->name);

		/* We have enough GPIOs that we'll overflow the output buffer
		 * without flushing */
		cflush();
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(gpioget, command_gpio_get,
			"[name]",
			"Read GPIO value(s)",
			NULL);


static int command_gpio_set(int argc, char **argv)
{
	const struct gpio_info *g;
	char *e;
	int v, i;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	i = find_signal_by_name(argv[1]);
	if (i == GPIO_COUNT)
		return EC_ERROR_PARAM1;
	g = gpio_list + i;

	if (!g->mask)
		return EC_ERROR_PARAM1;

	if (!(g->flags & GPIO_OUTPUT))
		return EC_ERROR_PARAM1;

	v = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	return gpio_set_level(i, v);
}
DECLARE_CONSOLE_COMMAND(gpioset, command_gpio_set,
			"name <0 | 1>",
			"Set a GPIO",
			NULL);

/*****************************************************************************/
/* Host commands */

static int gpio_command_get(struct host_cmd_handler_args *args)
{
	struct ec_params_gpio_get *p =
			(struct ec_params_gpio_get *)args->params;
	struct ec_response_gpio_get *r =
			(struct ec_response_gpio_get *)args->response;
	int i;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	i = find_signal_by_name(p->name);
	if (i == GPIO_COUNT)
		return EC_RES_ERROR;

	r->val = gpio_get_level(i);
	args->response_size = sizeof(struct ec_response_gpio_get);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GPIO_GET, gpio_command_get, EC_VER_MASK(0));


static int gpio_command_set(struct host_cmd_handler_args *args)
{
	struct ec_params_gpio_set *p =
			(struct ec_params_gpio_set *)args->params;
	int i;
	const struct gpio_info *g;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	i = find_signal_by_name(p->name);
	if (i == GPIO_COUNT)
		return EC_RES_ERROR;
	g = gpio_list + i;

	if (!g->mask)
		return EC_RES_ERROR;

	if (!(g->flags & GPIO_OUTPUT))
		return EC_RES_ERROR;

	if (gpio_set_level(i, p->val) != EC_SUCCESS)
		return EC_RES_ERROR;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GPIO_SET, gpio_command_set, EC_VER_MASK(0));
