/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "printf.h"
#include "util.h"

/****************************************************************************/
/* Obtain/release a RAM copy of the persistent variable store */

/*
 * NOTE: It would be nice to allocate this at need, but shared memory is
 * currently all or nothing and it's used elsewhere when writing to flash, so
 * we have to allocate it statically until/unless that changes.
 */
static uint8_t rbuf[CONFIG_FLASH_NVMEM_VARS_USER_SIZE];
static int rbuf_in_use;

test_mockable_static
void release_local_copy(void)
{
	rbuf_in_use = 0;
}

test_mockable_static
int get_local_copy(void)
{
	int rv;

	if (rbuf_in_use)
		return EC_SUCCESS;

	rbuf_in_use = 1;

	rv = nvmem_read(0, CONFIG_FLASH_NVMEM_VARS_USER_SIZE,
			rbuf, CONFIG_FLASH_NVMEM_VARS_USER_NUM);
	if (rv)
		release_local_copy();

	return rv;
}

/****************************************************************************/
/* Implementation notes
 *
 * The data_ member of struct tuple is simply the key and val blobs
 * concatenated together.
 *
 * We store the variable entries in flash (and RAM) using the struct tuple
 * defined in nvmem_vars.h. The entries are written sequentially with no
 * padding, starting at offset 0 of the CONFIG_FLASH_NVMEM_VARS_USER_NUM user
 * region. A single uint8_t zero byte will ALWAYS follow the valid entries.
 * Since valid entries have nonzero key_len, we can always detect the presence
 * of valid entries.
 *
 * A valid entry has both key_len and val_len between 1 and 255. The following
 * bytes represent these tuples: <"A","ab">, <"B","cde">:
 *
 *   Offset  Content   Meaning
 *    0      0x01      length of key
 *    1      0x02      length of val
 *    2      0x00      variable flags (unused at present)
 *    3      0x41      'A' (key)
 *    4      0x61      'a' (val byte 1)
 *    5      0x62      'b' (val byte 2)
 *    6      0x01      length of key
 *    7      0x03      length of val
 *    8      0x00      variable flags (unused at present)
 *    9      0x42      'B' (key)
 *    10     0x63      'c' (val byte 1)
 *    11     0x64      'd' (val byte 2)
 *    12     0x65      'e' (val byte 3)
 *    13     0x00      End of variables
 *
 * Note that the keys and values are not null-terminated since they're not
 * strings, just binary blobs. The length of each entry is the size of the
 * struct tuple header, plus the length of its key and value blobs.
 *
 * The .flags field is not currently used (and so is set to zero). It could be
 * used in the future to for per-variable attributes, such as read-only,
 * clear-on-reset, extended-length value, etc.
 */

/****************************************************************************/
/* Helper functions */

/* Return true if the tuple at rbuf+idx matches the key */
static int match_key_at(uint32_t idx, const uint8_t *key, uint8_t key_len)
{
	struct tuple *tuple = (struct tuple *)(rbuf + idx);
	uint32_t i, max_len;
	uint8_t diffs;

	/* Don't try to look past the 0 at the end of the user region */
	max_len = MIN(key_len, CONFIG_FLASH_NVMEM_VARS_USER_SIZE - idx - 1);

	/* Do constant-time comparision, since AP sets key_len to look for */
	diffs = max_len ^ key_len;
	diffs |= tuple->key_len ^ key_len;
	for (i = 0; i < max_len; i++)
		diffs |=  tuple->data_[i] ^ key[i];

	return !diffs;
}

/*
 * Find the start of the next tuple in rbuf. Return false if there isn't one.
 * The idx arg tracks where to start looking and where the next tuple was
 * expected to be found.
 */
static int next_tuple(uint32_t *idx)
{
	struct tuple *tuple = (struct tuple *)(rbuf + *idx);

	/* Not at a valid tuple now, so there aren't any more */
	if (!tuple->key_len)
		return 0;

	/* Advance to the next one */
	*idx += sizeof(struct tuple) + tuple->key_len + tuple->val_len;
	tuple = (struct tuple *)(rbuf + *idx);

	/* Do we have one or not? */
	return tuple->key_len;
}

/*
 * Look for the key in rbuf. If a match is found, set the index to the start of
 * the tuple and return true. If the key is not found, set the index to the
 * location where a new tuple should be added (0 if no tuples exist at all,
 * else at the '\0' at the end of the tuples) and return false.
 */
test_mockable_static
int getvar_idx(uint32_t *idx, const uint8_t *key, uint8_t key_len)
{
	uint32_t i = *idx;

	do {
		if (match_key_at(i, key, key_len)) {
			*idx = i;
			return 1;
		}
	} while (next_tuple(&i));

	*idx = i;
	return 0;
}

static inline int bogus_blob(const uint8_t *blob, uint8_t blob_len)
{
	return !blob || !blob_len;
}

/****************************************************************************/
/* API functions */

/* This MUST be called first. The internal functions assume valid entries */
int initvars(void)
{
	struct tuple *tuple;
	int rv, i, len;

	rv = get_local_copy();
	if (rv != EC_SUCCESS)
		return rv;

	for (i = len = 0; /* FOREVER */ 1; i += len) {
		tuple = (struct tuple *)(rbuf + i);

		/* Zero key_len indicates end of tuples, we're done */
		if (!tuple->key_len)
			break;

		/* Empty values are not allowed */
		if (!tuple->val_len)
			goto fixit;

		/* See how big the tuple is */
		len = sizeof(struct tuple) + tuple->key_len + tuple->val_len;

		/* Oops, it's off the end (leave one byte for final 0) */
		if (i + len >= CONFIG_FLASH_NVMEM_VARS_USER_SIZE)
			goto fixit;
	}

	/* Found the end of variables. Now make sure the rest is all 0xff. */
	for (i++ ; i < CONFIG_FLASH_NVMEM_VARS_USER_SIZE; i++)
		if (rbuf[i] != 0xff)
			goto fixit;

	release_local_copy();
	return EC_SUCCESS;

fixit:
	/* No variables */
	rbuf[0] = 0;
	/* Everything else is 0xff */
	memset(rbuf + 1, 0xff, CONFIG_FLASH_NVMEM_VARS_USER_SIZE - 1);

	return writevars();
}

const struct tuple *getvar(const uint8_t *key, uint8_t key_len)
{
	uint32_t i = 0;

	if (bogus_blob(key, key_len))
		return 0;

	if (get_local_copy() != EC_SUCCESS)
		return 0;

	if (!getvar_idx(&i, key, key_len))
		return 0;

	return (const struct tuple *)(rbuf + i);
}

const uint8_t *tuple_key(const struct tuple *t)
{
	return t->data_;
}

const uint8_t *tuple_val(const struct tuple *t)
{
	return t->data_ + t->key_len;
}

int setvar(const uint8_t *key, uint8_t key_len,
	   const uint8_t *val, uint8_t val_len)
{
	struct tuple *tuple;
	int rv, i, j;

	if (bogus_blob(key, key_len))
		return EC_ERROR_INVAL;

	rv = get_local_copy();
	if (rv != EC_SUCCESS)
		return rv;

	i = 0;
	if (getvar_idx(&i, key, key_len)) {
		/* Found the match at position i */
		j = i;
		if (next_tuple(&j)) {
			/*
			 * Now j is the start of the tuple after ours. Delete
			 * our entry by shifting left from there to the end of
			 * rbuf, so that it covers ours up.
			 *
			 * Before:
			 *            i        j
			 *   <foo,bar><KEY,VAL><hey,splat>0
			 *
			 * After:
			 *            i
			 *   <foo,bar><hey,splat>0...
			 */
			memmove(rbuf + i, rbuf + j,
				CONFIG_FLASH_NVMEM_VARS_USER_SIZE - j);
			/* Advance i to point to the end of all tuples */
			while (next_tuple(&i))
				;
		}
		/* Whether we found a match or not, it's not there now */
	}
	/*
	 * Now i is where the new tuple should be written.
	 *
	 * Either this:
	 *                       i
	 *   <foo,bar><hey,splat>0
	 *
	 * or there are no tuples at all and i == 0
	 *
	 */

	/* If there's no value to save, we're done. */
	if (bogus_blob(val, val_len))
		goto done;

	/*
	 * We'll always write the updated entry at the end of any existing
	 * tuples, and we mark the end with an additional 0. Make sure all that
	 * will all fit. If it doesn't, we've already removed the previous
	 * entry but we still need to mark the end.
	 */
	if (i + sizeof(struct tuple) + key_len + val_len + 1 >
	    CONFIG_FLASH_NVMEM_VARS_USER_SIZE) {
		rv = EC_ERROR_OVERFLOW;
		goto done;
	}

	/* write the tuple */
	tuple = (struct tuple *)(rbuf + i);
	tuple->key_len = key_len;
	tuple->val_len = val_len;
	tuple->flags = 0;			/* UNUSED, set to zero */
	memcpy(tuple->data_, key, key_len);
	memcpy(tuple->data_ + key_len, val, val_len);
	/* move past it */
	next_tuple(&i);

done:
	/* mark the end */
	rbuf[i++] = 0;
	/* erase the rest */
	memset(rbuf + i, 0xff, CONFIG_FLASH_NVMEM_VARS_USER_SIZE - i);

	return rv;
}

int writevars(void)
{
	int rv;

	if (!rbuf_in_use)
		return EC_SUCCESS;

	rv = nvmem_write(0, CONFIG_FLASH_NVMEM_VARS_USER_SIZE,
			 rbuf, CONFIG_FLASH_NVMEM_VARS_USER_NUM);
	if (rv != EC_SUCCESS)
		return rv;

	rv = nvmem_commit();
	if (rv != EC_SUCCESS)
		return rv;

	release_local_copy();

	return rv;
}

/****************************************************************************/
#ifdef TEST_BUILD
#include "console.h"

static void print_blob(const uint8_t *blob, int blob_len)
{
	int i;

	for (i = 0; i < blob_len; i++)
		ccprintf("%c", isprint(blob[i]) ? blob[i] : '.');
}

static int command_get(int argc, char **argv)
{
	const struct tuple *tuple;

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	tuple = getvar(argv[1], strlen(argv[1]));
	if (!tuple)
		return EC_SUCCESS;

	print_blob(tuple_val(tuple), tuple->val_len);
	ccprintf("\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(get, command_get,
			"VARIABLE",
			"Show the value of the specified variable");

static int command_set(int argc, char **argv)
{
	int rc;

	if (argc != 2 && argc != 3)
		return EC_ERROR_PARAM_COUNT;

	if (argc == 2)
		rc =  setvar(argv[1], strlen(argv[1]), 0, 0);
	else
		rc =  setvar(argv[1], strlen(argv[1]),
			     argv[2], strlen(argv[2]));
	if (rc)
		return rc;

	return writevars();
}
DECLARE_CONSOLE_COMMAND(set, command_set,
			"VARIABLE [VALUE]",
			"Set/clear the value of the specified variable");

static int command_print(int argc, char **argv)
{
	const struct tuple *tuple;
	int rv, i = 0;

	rv = get_local_copy();
	if (rv)
		return rv;

	tuple = (const struct tuple *)(rbuf + i);
	if (!tuple->key_len)
		return EC_SUCCESS;

	do {
		tuple = (const struct tuple *)(rbuf + i);
		print_blob(tuple_key(tuple), tuple->key_len);
		ccprintf("=");
		print_blob(tuple_val(tuple), tuple->val_len);
		ccprintf("\n");
	} while (next_tuple(&i));

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(print, command_print,
			"",
			"Print all defined variables");

static int command_dump(int argc, char **argv)
{
	int i, rv;

	rv = get_local_copy();
	if (rv)
		return rv;

	for (i = 0; i < CONFIG_FLASH_NVMEM_VARS_USER_SIZE; i++)
		ccprintf(" %02x", rbuf[i]);
	ccprintf("\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dump, command_dump,
			"",
			"Dump the variable memory");
#endif
