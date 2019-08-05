/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IO Expander Controller Common Code */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "ioexpander.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ## args)
#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)

static uint8_t last_val[(IOEX_COUNT + 7) / 8];

static int last_val_changed(int i, int v)
{
	if (v && !(last_val[i / 8] & (BIT(i % 8)))) {
		last_val[i / 8] |= BIT(i % 8);
		return 1;
	} else if (!v && last_val[i / 8] & (BIT(i % 8))) {
		last_val[i / 8] &= ~(BIT(i % 8));
		return 1;
	} else {
		return 0;
	}
}

static int ioex_is_valid_interrupt_signal(enum ioex_signal signal)
{
	const struct ioexpander_drv *drv;
	const struct ioex_info *g = ioex_list + signal;

	/* Fail if no interrupt handler */
	if (signal >= ioex_ih_count)
		return EC_ERROR_PARAM1;

	drv = ioex_config[g->ioex].drv;
	/*
	 * Not every IOEX chip can support interrupt, check it before enabling
	 * the interrupt function
	 */
	if (drv->enable_interrupt == NULL) {
		CPRINTS("IOEX chip port %d doesn't support INT", g->ioex);
		return EC_ERROR_UNIMPLEMENTED;
	}

	return EC_SUCCESS;
}
int ioex_enable_interrupt(enum ioex_signal signal)
{
	int rv;
	const struct ioex_info *g = ioex_list + signal;
	const struct ioexpander_drv *drv;

	rv = ioex_is_valid_interrupt_signal(signal);
	if (rv != EC_SUCCESS)
		return  rv;

	drv = ioex_config[g->ioex].drv;
	return drv->enable_interrupt(g->ioex, g->port, g->mask, 1);
}

int ioex_disable_interrupt(enum ioex_signal signal)
{
	int rv;
	const struct ioexpander_drv *drv;
	const struct ioex_info *g = ioex_list + signal;

	rv = ioex_is_valid_interrupt_signal(signal);
	if (rv != EC_SUCCESS)
		return  rv;

	drv = ioex_config[g->ioex].drv;
	return drv->enable_interrupt(g->ioex, g->port, g->mask, 0);
}

int ioex_get_flags_by_mask(int ioex, int port, int mask, int *flags)
{
	return ioex_config[ioex].drv->get_flags_by_mask(ioex, port, mask,
							flags);
}

int ioex_set_flags_by_mask(int ioex, int port, int mask, int flags)
{
	return ioex_config[ioex].drv->set_flags_by_mask(ioex, port, mask,
							flags);
}

int ioex_get_flags(enum ioex_signal signal, int *flags)
{
	const struct ioex_info *g = ioex_list + signal;

	return ioex_config[g->ioex].drv->get_flags_by_mask(g->ioex,
						g->port, g->mask, flags);
}

int ioex_set_flags(enum ioex_signal signal, int flags)
{
	const struct ioex_info *g = ioex_list + signal;

	return ioex_config[g->ioex].drv->set_flags_by_mask(g->ioex,
						g->port, g->mask, flags);
}

int ioex_get_level(enum ioex_signal signal, int *val)
{
	const struct ioex_info *g = ioex_list + signal;

	return ioex_config[g->ioex].drv->get_level(g->ioex, g->port,
							g->mask, val);
}

int ioex_set_level(enum ioex_signal signal, int value)
{
	const struct ioex_info *g = ioex_list + signal;

	return ioex_config[g->ioex].drv->set_level(g->ioex, g->port,
							g->mask, value);
}

int ioex_init(int ioex)
{
	const struct ioexpander_drv *drv = ioex_config[ioex].drv;

	if (drv->init == NULL)
		return EC_SUCCESS;

	return  drv->init(ioex);
}

static void ioex_init_default(void)
{
	const struct ioex_info *g = ioex_list;
	int i;

	for (i = 0; i < CONFIG_IO_EXPANDER_PORT_COUNT; i++)
		ioex_init(i);
	/*
	 * Set all IO expander GPIOs to default flags according to the setting
	 * in gpio.inc
	 */
	for (i = 0; i < IOEX_COUNT; i++, g++) {
		if (g->mask && !(g->flags & GPIO_DEFAULT)) {
			ioex_set_flags_by_mask(g->ioex, g->port,
						g->mask, g->flags);
		}
	}

}
DECLARE_HOOK(HOOK_INIT, ioex_init_default, HOOK_PRIO_INIT_I2C + 1);

const char *ioex_get_name(enum ioex_signal signal)
{
	return ioex_list[signal].name;
}

static void print_ioex_info(int io)
{
	int changed, v, val;
	int flags = 0;

	v = ioex_get_level(io, &val);
	if (v) {
		ccprintf("Fail to get %s level\n", ioex_get_name(io));
		return;
	}
	v = ioex_get_flags(io, &flags);
	if (v) {
		ccprintf("Fail to get %s flags\n", ioex_get_name(io));
		return;
	}

	changed = last_val_changed(io, val);

	ccprintf("  %d%c %s%s%s%s%s%s\n", val,
		 (changed ? '*' : ' '),
		 (flags & GPIO_INPUT ? "I " : ""),
		 (flags & GPIO_OUTPUT ? "O " : ""),
		 (flags & GPIO_LOW ? "L " : ""),
		 (flags & GPIO_HIGH ? "H " : ""),
		 (flags & GPIO_OPEN_DRAIN ? "ODR " : ""),
		ioex_get_name(io));

	/* Flush console to avoid truncating output */
	cflush();
}

int ioex_get_default_flags(enum ioex_signal signal)
{
	return ioex_list[signal].flags;
}

/* IO expander commands */
static enum ioex_signal find_ioex_by_name(const char *name)
{
	int i;

	if (!name)
		return IOEX_COUNT;

	for (i = 0; i < IOEX_COUNT; i++) {
		if (!strcasecmp(name, ioex_get_name(i)))
			return i;
	}

	return IOEX_COUNT;
}

static enum ec_error_list ioex_set(const char *name, int value)
{
	enum ioex_signal signal = find_ioex_by_name(name);

	if (signal == IOEX_COUNT)
		return EC_ERROR_INVAL;

	if (!(ioex_get_default_flags(signal) & GPIO_OUTPUT))
		return EC_ERROR_INVAL;

	return ioex_set_level(signal, value);
}

static int command_ioex_set(int argc, char **argv)
{
	char *e;
	int v;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	v = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	if (ioex_set(argv[1], v) != EC_SUCCESS)
		return EC_ERROR_PARAM1;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ioexset, command_ioex_set,
			"name <0 | 1>",
			"Set level of a IO expander IO");

static int command_ioex_get(int argc, char **argv)
{
	int i;

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		i = find_ioex_by_name(argv[1]);
		if (i == IOEX_COUNT)
			return EC_ERROR_PARAM1;
		print_ioex_info(i);

		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	for (i = 0; i < IOEX_COUNT; i++)
		print_ioex_info(i);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ioexget, command_ioex_get,
			     "[name]",
			     "Read level of IO expander pin(s)");

