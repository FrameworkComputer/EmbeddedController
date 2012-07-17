/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory commands for Chrome EC */

#include "console.h"
#include "flash.h"
#include "host_command.h"
#include "shared_mem.h"
#include "system.h"
#include "util.h"

/*
 * Parse offset and size from command line argv[shift] and argv[shift+1]
 *
 * Default values: If argc<=shift, leaves offset unchanged, returning error if
 * *offset<0.  If argc<shift+1, leaves size unchanged, returning error if
 * *size<0.
 */
static int parse_offset_size(int argc, char **argv, int shift,
			     int *offset, int *size)
{
	char *e;
	int i;

	if (argc > shift) {
		i = (uint32_t)strtoi(argv[shift], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		*offset = i;
	} else if (*offset < 0)
		return EC_ERROR_PARAM_COUNT;

	if (argc > shift + 1) {
		i = (uint32_t)strtoi(argv[shift + 1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		*size = i;
	} else if (*size < 0)
		return EC_ERROR_PARAM_COUNT;

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Console commands */

static int command_flash_info(int argc, char **argv)
{
	const uint8_t *wp;
	int banks = flash_get_size() / flash_get_protect_block_size();
	int i;

	ccprintf("Physical:%4d KB\n", flash_physical_size() / 1024);
	ccprintf("Usable:  %4d KB\n", flash_get_size() / 1024);
	ccprintf("Write:   %4d B\n", flash_get_write_block_size());
	ccprintf("Erase:   %4d B\n", flash_get_erase_block_size());
	ccprintf("Protect: %4d B\n", flash_get_protect_block_size());

	i = flash_get_protect_lock();
	ccprintf("Lock:    %s%s\n",
		 (i & FLASH_PROTECT_LOCK_SET) ? "LOCKED" : "unlocked",
		 (i & FLASH_PROTECT_LOCK_APPLIED) ? ",APPLIED" : "");
	ccprintf("WP pin:  %sasserted\n",
		 (i & FLASH_PROTECT_PIN_ASSERTED) ? "" : "de");

	wp = flash_get_protect_array();

	ccputs("Protected now:");
	for (i = 0; i < banks; i++) {
		if (!(i & 7))
			ccputs(" ");
		ccputs(wp[i] & FLASH_PROTECT_UNTIL_REBOOT ? "Y" : ".");
	}
	ccputs("\n  Persistent: ");
	for (i = 0; i < banks; i++) {
		if (!(i & 7))
			ccputs(" ");
		ccputs(wp[i] & FLASH_PROTECT_PERSISTENT ? "Y" : ".");
	}
	ccputs("\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flashinfo, command_flash_info,
			NULL,
			"Print flash info",
			NULL);


static int command_flash_erase(int argc, char **argv)
{
	int offset = -1;
	int size = flash_get_erase_block_size();
	int rv;

	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;

	ccprintf("Erasing %d bytes at 0x%x...\n", size, offset, offset);
	return flash_erase(offset, size);
}
DECLARE_CONSOLE_COMMAND(flasherase, command_flash_erase,
			"offset [size]",
			"Erase flash",
			NULL);

static int command_flash_write(int argc, char **argv)
{
	int offset = -1;
	int size = flash_get_erase_block_size();
	int rv;
	char *data;
	int i;


	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;

	if (size > shared_mem_size())
		size = shared_mem_size();

        /* Acquire the shared memory buffer */
	rv = shared_mem_acquire(size, 0, &data);
	if (rv) {
		ccputs("Can't get shared mem\n");
		return rv;
	}

	/* Fill the data buffer with a pattern */
	for (i = 0; i < size; i++)
		data[i] = i;

	ccprintf("Writing %d bytes to 0x%x...\n",
		 size, offset, offset);
	rv = flash_write(offset, size, data);

	/* Free the buffer */
	shared_mem_release(data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(flashwrite, command_flash_write,
			"offset [size]",
			"Write pattern to flash",
			NULL);

static int command_flash_wp(int argc, char **argv)
{
	int offset = -1;
	int size = flash_get_protect_block_size();
	int rv;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* Commands that don't need offset and size */
	if (!strcasecmp(argv[1], "lock"))
		return flash_lock_protect(1);
	else if (!strcasecmp(argv[1], "unlock"))
		return flash_lock_protect(0);

	/* All remaining commands need offset and size */
	rv = parse_offset_size(argc, argv, 2, &offset, &size);
	if (rv)
		return rv;

	if (!strcasecmp(argv[1], "now"))
		return flash_protect_until_reboot(offset, size);
	else if (!strcasecmp(argv[1], "set"))
		return flash_set_protect(offset, size, 1);
	else if (!strcasecmp(argv[1], "clear"))
		return flash_set_protect(offset, size, 0);
	else
		return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND(flashwp, command_flash_wp,
			"<lock | unlock | now | set | clear> offset [size]",
			"Print or modify flash write protect",
			NULL);

/*****************************************************************************/
/* Host commands */

static int flash_command_get_info(struct host_cmd_handler_args *args)
{
	struct ec_response_flash_info *r =
		(struct ec_response_flash_info *)args->response;

	r->flash_size = flash_get_size();
	r->write_block_size = flash_get_write_block_size();
	r->erase_block_size = flash_get_erase_block_size();
	r->protect_block_size = flash_get_protect_block_size();
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_INFO,
		     flash_command_get_info,
		     EC_VER_MASK(0));

static int flash_command_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_read *p =
		(const struct ec_params_flash_read *)args->params;

	if (flash_dataptr(p->offset, p->size, 1, (char **)&args->response) < 0)
		return EC_RES_ERROR;

	args->response_size = p->size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_READ,
		     flash_command_read,
		     EC_VER_MASK(0));

static int flash_command_write(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_write *p =
		(const struct ec_params_flash_write *)args->params;

	if (p->size > sizeof(p->data))
		return EC_RES_INVALID_PARAM;

	if (system_unsafe_to_overwrite(p->offset, p->size))
		return EC_RES_ACCESS_DENIED;

	if (flash_write(p->offset, p->size, p->data))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_WRITE,
		     flash_command_write,
		     EC_VER_MASK(0));

static int flash_command_erase(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_erase *p =
		(const struct ec_params_flash_erase *)args->params;

	if (system_unsafe_to_overwrite(p->offset, p->size))
		return EC_RES_ACCESS_DENIED;

	if (flash_erase(p->offset, p->size))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_ERASE,
		     flash_command_erase,
		     EC_VER_MASK(0));
