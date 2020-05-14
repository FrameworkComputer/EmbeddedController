/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IO Expander Controller Common Code */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "ioexpander.h"
#include "system.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ## args)
#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)

static uint8_t last_val[(IOEX_COUNT + 7) / 8];

static int last_val_changed(enum ioex_signal signal, int v)
{
	const int i = signal - IOEX_SIGNAL_START;

	ASSERT(signal_is_ioex(signal));

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

int signal_is_ioex(int signal)
{
	return ((signal >= IOEX_SIGNAL_START) && (signal < IOEX_SIGNAL_END));
}

static const struct ioex_info *ioex_get_signal_info(enum ioex_signal signal)
{
	const struct ioex_info *g;

	ASSERT(signal_is_ioex(signal));

	g = ioex_list + signal - IOEX_SIGNAL_START;

	if (ioex_config[g->ioex].flags & IOEX_FLAGS_DISABLED) {
		CPRINTS("ioex %s disabled", g->name);
		return NULL;
	}

	return g;
}

static int ioex_is_valid_interrupt_signal(enum ioex_signal signal)
{
	const struct ioexpander_drv *drv;
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_BUSY;

	/* Fail if no interrupt handler */
	if (signal - IOEX_SIGNAL_START >= ioex_ih_count)
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
	const struct ioex_info *g = ioex_get_signal_info(signal);
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
	const struct ioex_info *g = ioex_get_signal_info(signal);

	rv = ioex_is_valid_interrupt_signal(signal);
	if (rv != EC_SUCCESS)
		return  rv;

	drv = ioex_config[g->ioex].drv;
	return drv->enable_interrupt(g->ioex, g->port, g->mask, 0);
}

int ioex_get_flags(enum ioex_signal signal, int *flags)
{
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_BUSY;

	return ioex_config[g->ioex].drv->get_flags_by_mask(g->ioex,
						g->port, g->mask, flags);
}

int ioex_set_flags(enum ioex_signal signal, int flags)
{
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_BUSY;

	return ioex_config[g->ioex].drv->set_flags_by_mask(g->ioex,
						g->port, g->mask, flags);
}

int ioex_get_level(enum ioex_signal signal, int *val)
{
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_BUSY;

	return ioex_config[g->ioex].drv->get_level(g->ioex, g->port,
							g->mask, val);
}

int ioex_set_level(enum ioex_signal signal, int value)
{
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_BUSY;

	return ioex_config[g->ioex].drv->set_level(g->ioex, g->port,
							g->mask, value);
}

int ioex_init(int ioex)
{
	const struct ioex_info *g = ioex_list;
	const struct ioexpander_drv *drv = ioex_config[ioex].drv;
	int rv;
	int i;

	if (ioex_config[ioex].flags & IOEX_FLAGS_DISABLED)
		return EC_ERROR_BUSY;

	if (drv->init != NULL) {
		rv = drv->init(ioex);
		if (rv != EC_SUCCESS)
			return rv;
	}

	/*
	 * Set all IO expander GPIOs to default flags according to the setting
	 * in gpio.inc
	 */
	for (i = 0; i < IOEX_COUNT; i++, g++) {
		int flags = g->flags;

		if (g->ioex == ioex && g->mask && !(flags & GPIO_DEFAULT)) {
			/* Late-sysJump should not set the output levels */
			if (system_jumped_late())
				flags &= ~(GPIO_LOW | GPIO_HIGH);

			drv->set_flags_by_mask(g->ioex, g->port,
						g->mask, flags);
		}
	}

	return EC_SUCCESS;
}

static void ioex_init_default(void)
{
	int i;

	for (i = 0; i < CONFIG_IO_EXPANDER_PORT_COUNT; i++)
		ioex_init(i);
}
DECLARE_HOOK(HOOK_INIT, ioex_init_default, HOOK_PRIO_INIT_I2C + 1);

const char *ioex_get_name(enum ioex_signal signal)
{
	const struct ioex_info *g = ioex_list + signal - IOEX_SIGNAL_START;

	return g->name;
}

static void print_ioex_info(enum ioex_signal signal)
{
	int changed, v, val;
	int flags = 0;
	const struct ioex_info *g = ioex_list + signal - IOEX_SIGNAL_START;

	if (ioex_config[g->ioex].flags & IOEX_FLAGS_DISABLED) {
		ccprintf("  DISABLED %s\n", ioex_get_name(signal));
		return;
	}


	v = ioex_get_level(signal, &val);
	if (v) {
		ccprintf("Fail to get %s level\n", ioex_get_name(signal));
		return;
	}
	v = ioex_get_flags(signal, &flags);
	if (v) {
		ccprintf("Fail to get %s flags\n", ioex_get_name(signal));
		return;
	}

	changed = last_val_changed(signal, val);

	ccprintf("  %d%c %s%s%s%s%s%s\n", val,
		 (changed ? '*' : ' '),
		 (flags & GPIO_INPUT ? "I " : ""),
		 (flags & GPIO_OUTPUT ? "O " : ""),
		 (flags & GPIO_LOW ? "L " : ""),
		 (flags & GPIO_HIGH ? "H " : ""),
		 (flags & GPIO_OPEN_DRAIN ? "ODR " : ""),
		ioex_get_name(signal));

	/* Flush console to avoid truncating output */
	cflush();
}

static int ioex_get_default_flags(enum ioex_signal signal)
{
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return 0;

	return g->flags;
}

/* IO expander commands */
static enum ioex_signal find_ioex_by_name(const char *name)
{
	int i;

	if (!name)
		return -1;

	for (i = IOEX_SIGNAL_START; i < IOEX_SIGNAL_END; i++) {
		if (!strcasecmp(name, ioex_get_name(i)))
			return i;
	}

	return -1;
}

static enum ec_error_list ioex_set(const char *name, int value)
{
	enum ioex_signal signal = find_ioex_by_name(name);

	if (signal == -1)
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
		if (i == -1)
			return EC_ERROR_PARAM1;
		print_ioex_info(i);

		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	for (i = IOEX_SIGNAL_START; i < IOEX_SIGNAL_END; i++)
		print_ioex_info(i);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ioexget, command_ioex_get,
			     "[name]",
			     "Read level of IO expander pin(s)");

