/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "console.h"
#include "gpio.h"
#include "ioexpander.h"
#include "util.h"

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

static enum ioex_signal find_ioex_by_name(const char *name)
{
	enum ioex_signal signal;

	if (!name)
		return IOEX_SIGNAL_END;

	for (signal = IOEX_SIGNAL_START; signal < IOEX_SIGNAL_END; signal++) {
		if (!strcasecmp(name, ioex_get_name(signal)))
			return signal;
	}

	return IOEX_SIGNAL_END;
}

static void print_ioex_info(enum ioex_signal signal)
{
	int changed, v, val;
	int flags = 0;

	if (ioex_get_ioex_flags(signal, &flags)) {
		ccprintf("  ERROR getting flags\n");
		return;
	}

	if (!(flags & IOEX_FLAGS_INITIALIZED)) {
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

static enum ec_error_list ioex_set(const char *name, int value)
{
	enum ioex_signal signal = find_ioex_by_name(name);
	int flags;

	if (!signal_is_ioex(signal))
		return EC_ERROR_INVAL;

	if (ioex_get_flags(signal, &flags))
		return EC_ERROR_INVAL;

	if (!(flags & GPIO_OUTPUT))
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
			"Set level of a IO expander pin");

static int command_ioex_get(int argc, char **argv)
{
	enum ioex_signal signal;

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		signal = find_ioex_by_name(argv[1]);
		if (!signal_is_ioex(signal))
			return EC_ERROR_PARAM1;
		print_ioex_info(signal);

		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	for (signal = IOEX_SIGNAL_START; signal < IOEX_SIGNAL_END; signal++)
		print_ioex_info(signal);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ioexget, command_ioex_get,
			     "[name]",
			     "Read level of IO expander pin(s)");
