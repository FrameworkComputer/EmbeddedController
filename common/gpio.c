/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO common functionality for Chrome EC */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "system.h"
#include "util.h"

static uint8_t last_val[(GPIO_COUNT + 7) / 8];

/**
 * Find a GPIO signal by name.
 *
 * @param name		Signal name to find
 *
 * @return the signal index, or GPIO_COUNT if no match.
 */
static enum gpio_signal find_signal_by_name(const char *name)
{
	const struct gpio_info *g = gpio_list;
	int i;

	if (!name || !*name)
		return GPIO_COUNT;

	for (i = 0; i < GPIO_COUNT; i++, g++) {
		if (g->mask && !strcasecmp(name, g->name))
			return i;
	}

	return GPIO_COUNT;
}

/**
 * Update last_val
 *
 * @param i		Index of last_val[] to update
 * @param v		New value for last_val[i]
 *
 * @return 1 if last_val[i] was updated, 0 if last_val[i]==v.
 */
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

/*
 * GPIO_CONFIG_ALL_PORTS signifies a "don't care" for the GPIO port.  This is
 * used in gpio_config_pins().  When the port parameter is set to this, the
 * pin_mask parameter is ignored.
 */
#define GPIO_CONFIG_ALL_PORTS 0xFFFFFFFF

static int gpio_config_pins(enum module_id id,
			    uint32_t port,
			    uint32_t pin_mask,
			    int enable)
{
	const struct gpio_alt_func *af;
	int rv = EC_ERROR_INVAL;

	/* Find pins and set to alternate functions */
	for (af = gpio_alt_funcs; af < gpio_alt_funcs + gpio_alt_funcs_count;
	     af++) {
		if (af->module_id != id)
			continue;  /* Pins for some other module */

		/* Check to see if the requested port matches. */
		if ((port != GPIO_CONFIG_ALL_PORTS) && (port != af->port))
			continue;

		/* If we don't care which port, enable all applicable pins. */
		if (port == GPIO_CONFIG_ALL_PORTS)
			pin_mask = af->mask;

		if ((af->mask & pin_mask) == pin_mask) {
			if (!(af->flags & GPIO_DEFAULT))
				gpio_set_flags_by_mask(
					af->port,
					(af->mask & pin_mask),
					enable ? af->flags : GPIO_INPUT);
			gpio_set_alternate_function(
				af->port,
				(af->mask & pin_mask),
				enable ? af->func : -1);
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
	return gpio_config_pins(id,
				gpio_list[signal].port,
				gpio_list[signal].mask,
				enable);
}

void gpio_set_flags(enum gpio_signal signal, int flags)
{
	const struct gpio_info *g = gpio_list + signal;

	gpio_set_flags_by_mask(g->port, g->mask, flags);
}

void gpio_reset(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;

	gpio_set_flags_by_mask(g->port, g->mask, g->flags);
	gpio_set_alternate_function(g->port, g->mask, -1);
}

const char *gpio_get_name(enum gpio_signal signal)
{
	return gpio_list[signal].name;
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

		/* Flush console to avoid truncating output */
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

	gpio_set_level(i, v);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(gpioset, command_gpio_set,
			"name <0 | 1>",
			"Set a GPIO",
			NULL);

/*****************************************************************************/
/* Host commands */

static int gpio_command_get(struct host_cmd_handler_args *args)
{
	const struct gpio_info *g = gpio_list;
	const struct ec_params_gpio_get_v1 *p_v1 = args->params;
	struct ec_response_gpio_get_v1 *r_v1 = args->response;
	int i, len;

	if (args->version == 0) {
		const struct ec_params_gpio_get *p = args->params;
		struct ec_response_gpio_get *r = args->response;
		i = find_signal_by_name(p->name);
		if (i == GPIO_COUNT)
			return EC_RES_ERROR;

		r->val = gpio_get_level(i);
		args->response_size = sizeof(struct ec_response_gpio_get);
		return EC_RES_SUCCESS;
	}

	switch (p_v1->subcmd) {
	case EC_GPIO_GET_BY_NAME:
		i = find_signal_by_name(p_v1->get_value_by_name.name);
		if (i == GPIO_COUNT)
			return EC_RES_ERROR;

		r_v1->get_value_by_name.val = gpio_get_level(i);
		args->response_size = sizeof(r_v1->get_value_by_name);
		break;
	case EC_GPIO_GET_COUNT:
		r_v1->get_count.val = GPIO_COUNT;
		args->response_size = sizeof(r_v1->get_count);
		break;
	case EC_GPIO_GET_INFO:
		if (p_v1->get_info.index >= GPIO_COUNT)
			return EC_RES_ERROR;

		i = p_v1->get_info.index;
		len = strlen(g[i].name);
		memcpy(r_v1->get_info.name, g[i].name, len+1);
		r_v1->get_info.val = gpio_get_level(i);
		r_v1->get_info.flags = g[i].flags;
		args->response_size = sizeof(r_v1->get_info);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;

}
DECLARE_HOST_COMMAND(EC_CMD_GPIO_GET, gpio_command_get,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static int gpio_command_set(struct host_cmd_handler_args *args)
{
	const struct ec_params_gpio_set *p = args->params;
	const struct gpio_info *g;
	int i;

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

	gpio_set_level(i, p->val);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GPIO_SET, gpio_command_set, EC_VER_MASK(0));
