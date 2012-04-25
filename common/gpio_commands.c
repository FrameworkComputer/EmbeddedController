/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO console commands for Chrome EC */

#include "board.h"
#include "console.h"
#include "gpio.h"
#include "util.h"


/* Signal information from board.c.  Must match order from enum gpio_signal. */
extern const struct gpio_info gpio_list[GPIO_COUNT];


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


static uint8_t last_val[(GPIO_COUNT + 7) / 8];

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

static int command_gpio_get(int argc, char **argv)
{
	const struct gpio_info *g = gpio_list;
	int changed, v, i;

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		i = find_signal_by_name(argv[1]);
		if (i == GPIO_COUNT) {
			ccputs("Unknown signal name.\n");
			return EC_ERROR_UNKNOWN;
		}
		g = gpio_list + i;
		v = gpio_get_level(i);
		changed = last_val_changed(i, v);
		ccprintf("  %d%c %s\n", v, (changed ? '*' : ' '), g->name);

		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	ccputs("Current GPIO levels:\n");
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
DECLARE_CONSOLE_COMMAND(gpioget, command_gpio_get);


static int command_gpio_set(int argc, char **argv)
{
	const struct gpio_info *g;
	char *e;
	int v, i;

	if (argc < 3) {
		ccputs("Usage: gpioset <signal_name> <0|1>\n");
		return EC_ERROR_UNKNOWN;
	}

	i = find_signal_by_name(argv[1]);
	if (i == GPIO_COUNT) {
		ccputs("Unknown signal name.\n");
		return EC_ERROR_UNKNOWN;
	}
	g = gpio_list + i;

	if (!g->mask) {
		ccputs("Signal is not implemented.\n");
		return EC_ERROR_UNKNOWN;
	}
	if (!(g->flags & GPIO_OUTPUT)) {
		ccputs("Signal is not an output.\n");
		return EC_ERROR_UNKNOWN;
	}

	v = strtoi(argv[2], &e, 0);
	if (*e) {
		ccputs("Invalid signal value.\n");
		return EC_ERROR_UNKNOWN;
	}

	return gpio_set_level(i, v);
}
DECLARE_CONSOLE_COMMAND(gpioset, command_gpio_set);
