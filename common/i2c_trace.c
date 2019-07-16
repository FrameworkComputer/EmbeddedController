/* Copyright 2019 The Chromium OS Authors. All rights reserved.
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
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

struct i2c_trace_range {
	bool enabled;
	int port;
	int slave_addr_lo; /* Inclusive */
	int slave_addr_hi; /* Inclusive */
};

static struct i2c_trace_range trace_entries[8];

void i2c_trace_notify(int port, uint16_t slave_addr_flags,
		      int direction, const uint8_t *data, size_t size)
{
	size_t i;
	uint16_t addr = I2C_GET_ADDR(slave_addr_flags);

	for (i = 0; i < ARRAY_SIZE(trace_entries); i++)
		if (trace_entries[i].enabled
		    && trace_entries[i].port == port
		    && trace_entries[i].slave_addr_lo <= addr
		    && trace_entries[i].slave_addr_hi >= addr)
			goto trace_enabled;
	return;

trace_enabled:
	CPRINTF("i2c: %s %d:0x%X ",
		direction ? "read" : "write",
		port,
		addr);
	for (i = 0; i < size; i++)
		CPRINTF("%02X ", data[i]);
	CPRINTF("\n");
}

static int command_i2ctrace_list(void)
{
	size_t i;

	ccprintf("id port address\n");
	ccprintf("-- ---- -------\n");

	for (i = 0; i < ARRAY_SIZE(trace_entries); i++) {
		if (trace_entries[i].enabled) {
			ccprintf("%2d %4d 0x%X",
				 i,
				 trace_entries[i].port,
				 trace_entries[i].slave_addr_lo);
			if (trace_entries[i].slave_addr_hi
			    != trace_entries[i].slave_addr_lo)
				ccprintf(" to 0x%X",
					 trace_entries[i].slave_addr_hi);
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

static int command_i2ctrace_enable(int port, int slave_addr_lo,
				   int slave_addr_hi)
{
	struct i2c_trace_range *t;
	struct i2c_trace_range *new_entry = NULL;

	if (port >= i2c_ports_used)
		return EC_ERROR_PARAM2;

	if (slave_addr_lo > slave_addr_hi)
		return EC_ERROR_PARAM3;

	/*
	 * Scan thru existing entries to see if there is one we can
	 * extend instead of making a new entry
	 */
	for (t = trace_entries;
	     t < trace_entries + ARRAY_SIZE(trace_entries);
	     t++) {
		if (t->enabled && t->port == port) {
			/* Subset of existing range, do nothing */
			if (t->slave_addr_lo <= slave_addr_lo &&
			    t->slave_addr_hi >= slave_addr_hi)
				return EC_SUCCESS;

			/* Extends exising range on both directions, replace */
			if (t->slave_addr_lo >= slave_addr_lo &&
			    t->slave_addr_hi <= slave_addr_hi) {
				t->enabled = 0;
				return command_i2ctrace_enable(
					port, slave_addr_lo, slave_addr_hi);
			}

			/* Extends existing range below */
			if (t->slave_addr_lo - 1 <= slave_addr_hi &&
			    t->slave_addr_hi >= slave_addr_hi) {
				t->enabled = 0;
				return command_i2ctrace_enable(
					port,
					slave_addr_lo,
					t->slave_addr_hi);
			}

			/* Extends existing range above */
			if (t->slave_addr_lo <= slave_addr_lo &&
			    t->slave_addr_hi + 1 >= slave_addr_lo) {
				t->enabled = 0;
				return command_i2ctrace_enable(
					port,
					t->slave_addr_lo,
					slave_addr_hi);
			}
		} else if (!t->enabled && !new_entry) {
			new_entry = t;
		}
	}

	/* We need to allocate a new entry */
	if (new_entry) {
		new_entry->enabled = 1;
		new_entry->port = port;
		new_entry->slave_addr_lo = slave_addr_lo;
		new_entry->slave_addr_hi = slave_addr_hi;
		return EC_SUCCESS;
	}

	ccprintf("No space to allocate new trace entry. Delete some first.\n");
	return EC_ERROR_MEMORY_ALLOCATION;
}


static int command_i2ctrace(int argc, char **argv)
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

		return command_i2ctrace_enable(
			id_or_port, address_low, address_high);
	}

	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND(i2ctrace,
			command_i2ctrace,
			"[list | disable <id> | enable <port> <address> | "
			"enable <port> <address-low> <address-high>]",
			"Trace I2C transactions");
