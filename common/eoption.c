/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Persistent EC options stored in EEPROM */

#include "console.h"
#include "eeprom.h"
#include "eoption.h"
#include "util.h"

#define EOPTION_MAGIC 0x456f       /* Magic number for header 'Eo' */
#define EOPTION_VERSION_CURRENT 1  /* Current version of options data */

struct eoption_bool_data {
	int offset;        /* Word offset of option */
	int mask;          /* Option mask within byte */
	const char *name;  /* Option name */
};


/* Word offsets inside the EOPTION EEPROM block */
enum block_offsets {
	OFFSET_HEADER = 0,
	OFFSET_BOOL0,
};

/*
 * Boolean options.  Must be in the same order as enum eoption_bool, and must
 * be terminated by an entry with a NULL name.
 */
static const struct eoption_bool_data bool_opts[] = {
	{OFFSET_BOOL0, (1 << 0), "bool_test"},
	{0, 0, NULL},
};

/**
 * Read a uint32_t from the specified EEPROM word offset.
 */
static int read32(int offset, uint32_t *dest)
{
	return eeprom_read(EEPROM_BLOCK_EOPTION, offset * 4, sizeof(uint32_t),
			   (char *)dest);
}


/**
 * Write a uint32_t to the specified EEPROM word offset.
 */
static int write32(int offset, uint32_t v)
{
	return eeprom_write(EEPROM_BLOCK_EOPTION, offset * 4, sizeof(v),
			    (char *)&v);
}

int eoption_get_bool(enum eoption_bool opt)
{
	const struct eoption_bool_data *d = bool_opts + opt;
	uint32_t v = 0;

	read32(d->offset, &v);
	return v & d->mask ? 1 : 0;
}

int eoption_set_bool(enum eoption_bool opt, int value)
{
	const struct eoption_bool_data *d = bool_opts + opt;
	uint32_t v;
	int rv;

	rv = read32(d->offset, &v);
	if (rv != EC_SUCCESS)
		return rv;

	if (value)
		v |= d->mask;
	else
		v &= ~d->mask;

	return write32(d->offset, v);
}

/**
 * Find an option by name.
 *
 * @return The option index, or -1 if no match.
 */
static int find_option_by_name(const char *name,
			       const struct eoption_bool_data *d)
{
	int i;

	if (!name || !*name)
		return -1;

	for (i = 0; d->name; i++, d++) {
		if (!strcasecmp(name, d->name))
			return i;
	}

	return -1;
}

void eoption_init(void)
{
	uint32_t v;
	int version;

	/* Initialize EEPROM if necessary */
	read32(OFFSET_HEADER, &v);

	/* Check header */
	if (v >> 16 != EOPTION_MAGIC)
		v = EOPTION_MAGIC << 16;  /* (implicitly sets version=0) */

	version = (v >> 8) & 0xff;
	if (version == EOPTION_VERSION_CURRENT)
		return;

	/*
	 * TODO: should have a CRC if we start using this for real
	 * (non-debugging) options.
	 */

	/* Initialize fields which weren't set in previous versions */
	if (version < 1)
		write32(OFFSET_BOOL0, 0);

	/* Update the header */
	v = (v & ~0xff00) | (EOPTION_VERSION_CURRENT << 8);
	write32(OFFSET_HEADER, v);
}

/*****************************************************************************/
/* Console commands */

static int command_eoption_get(int argc, char **argv)
{
	const struct eoption_bool_data *d;
	int i;

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		i = find_option_by_name(argv[1], bool_opts);
		if (i == -1)
			return EC_ERROR_PARAM1;
		d = bool_opts + i;
		ccprintf("  %d %s\n", eoption_get_bool(i), d->name);
		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	for (i = 0, d = bool_opts; d->name; i++, d++) {
		ccprintf("  %d %s\n", eoption_get_bool(i), d->name);

		/* We have enough options that we'll overflow the output buffer
		 * without flushing */
		cflush();
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(optget, command_eoption_get,
			"[name]",
			"Print EC option(s)",
			NULL);

static int command_eoption_set(int argc, char **argv)
{
	char *e;
	int v, i;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	v = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	i = find_option_by_name(argv[1], bool_opts);
	if (i == -1)
		return EC_ERROR_PARAM1;

	return eoption_set_bool(i, v);
}
DECLARE_CONSOLE_COMMAND(optset, command_eoption_set,
			"name value",
			"Set EC option",
			NULL);
