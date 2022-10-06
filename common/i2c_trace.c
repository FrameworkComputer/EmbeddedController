/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "i2c.h"
#include "stddef.h"
#include "stdbool.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ##args)

struct i2c_trace_range {
	bool enabled;
	int port;
	int addr_lo; /* Inclusive */
	int addr_hi; /* Inclusive */
};

static struct i2c_trace_range trace_entries[8];

void i2c_trace_notify(int port, uint16_t addr_flags, const uint8_t *out_data,
		      size_t out_size, const uint8_t *in_data, size_t in_size,
		      int ret)
{
	size_t i;
	uint16_t addr = I2C_STRIP_FLAGS(addr_flags);

	for (i = 0; i < ARRAY_SIZE(trace_entries); i++)
		if (trace_entries[i].enabled && trace_entries[i].port == port &&
		    trace_entries[i].addr_lo <= addr &&
		    trace_entries[i].addr_hi >= addr)
			goto trace_enabled;
	return;

trace_enabled:
	CPRINTF("i2c: %d:0x%X ", port, addr);
	if (out_size) {
		CPRINTF("wr ");
		for (i = 0; i < out_size; i++)
			CPRINTF("0x%02X ", out_data[i]);
	}
	if (ret != EC_SUCCESS) {
		CPRINTF(" error: %d", ret);
	} else if (in_size) {
		CPRINTF("  rd ");
		for (i = 0; i < in_size; i++)
			CPRINTF("0x%02X ", in_data[i]);
	}
	CPRINTF("\n");
}

static int command_i2ctrace_list(void)
{
	size_t i;
	const struct i2c_port_t *i2c_port;

	ccprintf("id port       address\n");
	ccprintf("-- ----       -------\n");

	for (i = 0; i < ARRAY_SIZE(trace_entries); i++) {
		if (trace_entries[i].enabled) {
			i2c_port = get_i2c_port(trace_entries[i].port);
			ccprintf("%-2zd %d %-8s 0x%X", i, trace_entries[i].port,
#ifndef CONFIG_ZEPHYR
				 i2c_port->name,
#else
				 "",
#endif /* CONFIG_ZEPHYR */
				 trace_entries[i].addr_lo);
			if (trace_entries[i].addr_hi !=
			    trace_entries[i].addr_lo)
				ccprintf(" to 0x%X", trace_entries[i].addr_hi);
			ccprintf("\n");
		}
	}

	return EC_SUCCESS;
}

static int command_i2ctrace_disable(size_t id)
{
	if (id >= ARRAY_SIZE(trace_entries))
		return EC_ERROR_PARAM2;

	trace_entries[id].enabled = 0;
	return EC_SUCCESS;
}

static int command_i2ctrace_enable(int port, int addr_lo, int addr_hi)
{
	struct i2c_trace_range *t;
	struct i2c_trace_range *new_entry = NULL;

	if (!get_i2c_port(port))
		return EC_ERROR_PARAM2;

	if (addr_lo > addr_hi)
		return EC_ERROR_PARAM3;

	/*
	 * Scan thru existing entries to see if there is one we can
	 * extend instead of making a new entry
	 */
	for (t = trace_entries; t < trace_entries + ARRAY_SIZE(trace_entries);
	     t++) {
		if (t->enabled && t->port == port) {
			/* Subset of existing range, do nothing */
			if (t->addr_lo <= addr_lo && t->addr_hi >= addr_hi)
				return EC_SUCCESS;

			/* Extends exising range on both directions, replace */
			if (t->addr_lo >= addr_lo && t->addr_hi <= addr_hi) {
				t->enabled = 0;
				return command_i2ctrace_enable(port, addr_lo,
							       addr_hi);
			}

			/* Extends existing range below */
			if (t->addr_lo - 1 <= addr_hi &&
			    t->addr_hi >= addr_hi) {
				t->enabled = 0;
				return command_i2ctrace_enable(port, addr_lo,
							       t->addr_hi);
			}

			/* Extends existing range above */
			if (t->addr_lo <= addr_lo &&
			    t->addr_hi + 1 >= addr_lo) {
				t->enabled = 0;
				return command_i2ctrace_enable(port, t->addr_lo,
							       addr_hi);
			}
		} else if (!t->enabled && !new_entry) {
			new_entry = t;
		}
	}

	/* We need to allocate a new entry */
	if (new_entry) {
		new_entry->enabled = 1;
		new_entry->port = port;
		new_entry->addr_lo = addr_lo;
		new_entry->addr_hi = addr_hi;
		return EC_SUCCESS;
	}

	ccprintf("No space to allocate new trace entry. Delete some first.\n");
	return EC_ERROR_MEMORY_ALLOCATION;
}

static int command_i2ctrace(int argc, const char **argv)
{
	int id_or_port;
	int address_low;
	int address_high;
	char *end;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "list") && argc == 2)
		return command_i2ctrace_list();

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	id_or_port = strtoi(argv[2], &end, 0);
	if (*end || id_or_port < 0)
		return EC_ERROR_PARAM2;

	if (!strcasecmp(argv[1], "disable") && argc == 3)
		return command_i2ctrace_disable(id_or_port);

	if (!strcasecmp(argv[1], "enable")) {
		address_low = strtoi(argv[3], &end, 0);
		if (*end || address_low < 0)
			return EC_ERROR_PARAM3;

		if (argc == 4) {
			address_high = address_low;
		} else if (argc == 5) {
			address_high = strtoi(argv[4], &end, 0);
			if (*end || address_high < 0)
				return EC_ERROR_PARAM4;
		} else {
			return EC_ERROR_PARAM_COUNT;
		}

		return command_i2ctrace_enable(id_or_port, address_low,
					       address_high);
	}

	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND(i2ctrace, command_i2ctrace,
			"[list | disable <id> | enable <port> <address> | "
			"enable <port> <address-low> <address-high>]",
			"Trace I2C transactions");
