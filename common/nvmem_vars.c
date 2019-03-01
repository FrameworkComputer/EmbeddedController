/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "nvmem.h"
#include "new_nvmem.h"
#include "printf.h"
#include "shared_mem.h"
#include "util.h"

/****************************************************************************/
/* Pointer to the RAM copy of the persistent variable store */

test_mockable_static uint8_t *rbuf;

int set_local_copy(void)
{
	if (rbuf)
		return EC_ERROR_UNKNOWN;

	rbuf = nvmem_cache_base(NVMEM_CR50);

	return EC_SUCCESS;
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
/* API functions */

const struct tuple *legacy_getnextvar(const struct tuple *prev_var)
{
	const struct tuple *var;
	uintptr_t idx;

	if (!prev_var) {
		/*
		 * The caller is just starting, let's get the first var, if
		 * any.
		 */
		if (!rbuf[0])
			return NULL;
		return (const struct tuple *)rbuf;
	}

	/* Let's try to get the next one. */
	idx = (uintptr_t)prev_var;
	idx += prev_var->key_len + prev_var->val_len + sizeof(struct tuple);

	var = (const struct tuple *)idx;

	if (var->key_len)
		return var;

	return NULL;
}

const uint8_t *tuple_key(const struct tuple *t) { return t->data_; }

const uint8_t *tuple_val(const struct tuple *t)
{
	return t->data_ + t->key_len;
}

/****************************************************************************/
#if defined(TEST_BUILD) && !defined(TEST_FUZZ)
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
		rc = setvar(argv[1], strlen(argv[1]), 0, 0);
	else
		rc = setvar(argv[1], strlen(argv[1]), argv[2], strlen(argv[2]));

	return rc;
}
DECLARE_CONSOLE_COMMAND(set, command_set, "VARIABLE [VALUE]",
			"Set/clear the value of the specified variable");

static int command_print(int argc, char **argv)
{
	ccprintf("Print all vars is not yet implemented\n");
	return EC_ERROR_INVAL;
}
DECLARE_CONSOLE_COMMAND(print, command_print, "",
			"Print all defined variables");

static int command_clear_nvmem_vars(int argc, char **argv)
{
	ccprintf("Nvmem clear vars has not yet been implemented\n");
	return EC_ERROR_INVAL;
}
DECLARE_CONSOLE_COMMAND(clr_nvmem_vars, command_clear_nvmem_vars, "",
			"Clear the NvMem variables.");
#endif
