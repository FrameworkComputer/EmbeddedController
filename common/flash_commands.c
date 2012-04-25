/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory commands for Chrome EC */

#include "console.h"
#include "flash.h"
#include "host_command.h"
#include "registers.h"  /* TODO: remove; only for temp debugging */
#include "shared_mem.h"
#include "system.h"
#include "util.h"

/* Parse offset and size from command line argv[0] and argv[1].
 *
 * Default values: If argc<1, leaves offset unchanged, returning error if
 * *offset<0.  If argc<2, leaves size unchanged, returning error if *size<0. */
static int parse_offset_size(int argc, char **argv, int *offset, int *size)
{
	char *e;
	int i;

	if (argc >= 1) {
		i = (uint32_t)strtoi(argv[0], &e, 0);
		if (e && *e) {
			ccprintf("Invalid offset \"%s\"\n", argv[0]);
			return EC_ERROR_INVAL;
		}
		*offset = i;
	} else if (*offset < 0) {
		ccputs("Must specify offset.\n");
		return EC_ERROR_INVAL;
	}

	if (argc >= 2) {
		i = (uint32_t)strtoi(argv[1], &e, 1);
		if (e && *e) {
			ccprintf("Invalid size \"%s\"\n", argv[1]);
			return EC_ERROR_INVAL;
		}
		*size = i;
	} else if (*size < 0) {
		ccputs("Must specify offset and size.\n");
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}


/*****************************************************************************/
/* Console commands */

static int command_flash_info(int argc, char **argv)
{
	const uint8_t *wp;
	int banks = flash_get_size() / flash_get_protect_block_size();
	int i;

	ccprintf("Physical size: %4d KB\n", flash_physical_size() / 1024);
	ccprintf("Usable size:   %4d KB\n", flash_get_size() / 1024);
	ccprintf("Write block:   %4d B\n", flash_get_write_block_size());
	ccprintf("Erase block:   %4d B\n", flash_get_erase_block_size());
	ccprintf("Protect block: %4d B\n", flash_get_protect_block_size());

	i = flash_get_protect_lock();
	ccprintf("Protect lock:  %s%s\n",
		 (i & FLASH_PROTECT_LOCK_SET) ? "LOCKED" : "unlocked",
		 (i & FLASH_PROTECT_LOCK_APPLIED) ? " AND APPLIED" : "");
	ccprintf("WP pin:        %s\n", (i & FLASH_PROTECT_PIN_ASSERTED) ?
		 "ASSERTED" : "deasserted");

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
DECLARE_CONSOLE_COMMAND(flashinfo, command_flash_info);


static int command_flash_erase(int argc, char **argv)
{
	int offset = -1;
	int size = flash_get_erase_block_size();
	int rv;

	rv = parse_offset_size(argc - 1, argv + 1, &offset, &size);
	if (rv)
		return rv;

	ccprintf("Erasing %d bytes at offset 0x%x (%d)...\n",
		    size, offset, offset);
	return flash_erase(offset, size);
}
DECLARE_CONSOLE_COMMAND(flasherase, command_flash_erase);


static int command_flash_write(int argc, char **argv)
{
	int offset = -1;
	int size = flash_get_erase_block_size();
	int rv;
	char *data;
	int i;


	rv = parse_offset_size(argc - 1, argv + 1, &offset, &size);
	if (rv)
		return rv;

	if (size > shared_mem_size()) {
		ccputs("Truncating size\n");
		size = shared_mem_size();
	}

        /* Acquire the shared memory buffer */
	rv = shared_mem_acquire(size, 0, &data);
	if (rv) {
		ccprintf("Unable to acquire %d byte buffer\n", size);
		return rv;
	}

	/* Fill the data buffer with a pattern */
	for (i = 0; i < size; i++)
		data[i] = i;

	ccprintf("Writing %d bytes to offset 0x%x (%d)...\n",
		 size, offset, offset);
	rv = flash_write(offset, size, data);
	if (rv == EC_SUCCESS)
		ccputs("done.\n");
	else
		ccprintf("failed. (error %d)\n", rv);

	/* Free the buffer */
	shared_mem_release(data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(flashwrite, command_flash_write);


static const char flash_wp_help[] =
	"Usage: flashwp <now | set | clear> <offset> [size]\n"
	"   or: flashwp <lock | unlock>\n";


static int command_flash_wp(int argc, char **argv)
{
	int offset = -1;
	int size = flash_get_protect_block_size();
	int rv;

	if (argc < 2) {
		ccputs(flash_wp_help);
		return EC_ERROR_INVAL;
	}

	/* Commands that don't need offset and size */
	if (!strcasecmp(argv[1], "lock"))
		return flash_lock_protect(1);
	else if (!strcasecmp(argv[1], "unlock"))
		return flash_lock_protect(0);

	/* All remaining commands need offset and size */
	rv = parse_offset_size(argc - 2, argv + 2, &offset, &size);
	if (rv)
		return rv;

	if (!strcasecmp(argv[1], "now"))
		return flash_protect_until_reboot(offset, size);
	else if (!strcasecmp(argv[1], "set"))
		return flash_set_protect(offset, size, 1);
	else if (!strcasecmp(argv[1], "clear"))
		return flash_set_protect(offset, size, 0);
	else {
		ccputs(flash_wp_help);
		return EC_ERROR_INVAL;
	}

}
DECLARE_CONSOLE_COMMAND(flashwp, command_flash_wp);

/*****************************************************************************/
/* Host commands */

enum lpc_status flash_command_get_info(uint8_t *data)
{
	struct lpc_response_flash_info *r =
			(struct lpc_response_flash_info *)data;

	r->flash_size = flash_get_size();
	r->write_block_size = flash_get_write_block_size();
	r->erase_block_size = flash_get_erase_block_size();
	r->protect_block_size = flash_get_protect_block_size();
	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_FLASH_INFO, flash_command_get_info);


#ifdef SUPPORT_CHECKSUM
enum lpc_status flash_command_checksum(uint8_t *data)
{
	struct lpc_params_flash_checksum *p =
	                (struct lpc_params_flash_checksum *)data;
	struct lpc_response_flash_checksum *r =
	                (struct lpc_response_flash_checksum *)data;
	uint8_t cs, byte;
	int j;

	for (cs = 0, j = 0; j < p->size; ++j) {
		if (flash_read(p->offset + j, 1, &byte))
			return EC_LPC_RESULT_ERROR;
		BYTE_IN(cs, byte);
	}

	r->checksum = cs;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_FLASH_CHECKSUM, flash_command_checksum);
#endif


enum lpc_status flash_command_read(uint8_t *data)
{
	struct lpc_params_flash_read *p =
			(struct lpc_params_flash_read *)data;
	struct lpc_response_flash_read *r =
			(struct lpc_response_flash_read *)data;

	if (p->size > sizeof(r->data))
		return EC_LPC_RESULT_ERROR;

	if (flash_read(p->offset, p->size, r->data))
		return EC_LPC_RESULT_ERROR;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_FLASH_READ, flash_command_read);


enum lpc_status flash_command_write(uint8_t *data)
{
	struct lpc_params_flash_write *p =
			(struct lpc_params_flash_write *)data;

	if (p->size > sizeof(p->data))
		return EC_LPC_RESULT_ERROR;

	if (system_unsafe_to_overwrite(p->offset, p->size))
		return EC_LPC_RESULT_ACCESS_DENIED;

	if (flash_write(p->offset, p->size, p->data))
		return EC_LPC_RESULT_ERROR;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_FLASH_WRITE, flash_command_write);


enum lpc_status flash_command_erase(uint8_t *data)
{
	struct lpc_params_flash_erase *p =
			(struct lpc_params_flash_erase *)data;

	if (system_unsafe_to_overwrite(p->offset, p->size))
		return EC_LPC_RESULT_ACCESS_DENIED;

	if (flash_erase(p->offset, p->size))
		return EC_LPC_RESULT_ERROR;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_FLASH_ERASE, flash_command_erase);


#ifdef REWORK_THESE_TO_USE_NEW_WP_MECHANISM
/* TODO: (crosbug.com/p/8448) rework these to use the new WP mechanism.  Note
 * that the concept of a single range oversimplifies how WP works; there are
 * multiple protected regions, including the persistent WP state (last bank)
 * and rollback information (bank before that; will be implemented by
 * vboot). */

static int shadow_wp_offset;
static int shadow_wp_size;

enum lpc_status flash_command_wp_enable(uint8_t *data)
{
	struct lpc_params_flash_wp_enable *p =
			(struct lpc_params_flash_wp_enable *)data;
	int offset, size;

	if (p->enable_wp) {
		offset = shadow_wp_offset;
		size   = shadow_wp_size;
	} else {
		offset = 0;
		size   = 0;
	}
	if (flash_set_write_protect_range(offset, size))
		return EC_LPC_RESULT_ERROR;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_FLASH_WP_ENABLE,
		     flash_command_wp_enable);


enum lpc_status flash_command_wp_get_state(uint8_t *data)
{
	struct lpc_response_flash_wp_enable *p =
			(struct lpc_response_flash_wp_enable *)data;

	if (flash_get_write_protect_status() & EC_FLASH_WP_RANGE_LOCKED)
		p->enable_wp = 1;
	else
		p->enable_wp = 0;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_FLASH_WP_GET_STATE,
		     flash_command_wp_get_state);


enum lpc_status flash_command_wp_set_range(uint8_t *data)
{
	struct lpc_params_flash_wp_range *p =
			(struct lpc_params_flash_wp_range *)data;

	if (flash_set_write_protect_range(p->offset, p->size))
		return EC_LPC_RESULT_ERROR;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_FLASH_WP_SET_RANGE,
		     flash_command_wp_set_range);


enum lpc_status flash_command_wp_get_range(uint8_t *data)
{
	struct lpc_response_flash_wp_range *p =
			(struct lpc_response_flash_wp_range *)data;

	if (flash_get_write_protect_range(&p->offset, &p->size))
		return EC_LPC_RESULT_ERROR;

	return EC_LPC_RESULT_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_LPC_COMMAND_FLASH_WP_GET_RANGE,
		     flash_command_wp_get_range);

#endif /* REWORK_THESE_TO_USE_NEW_WP_MECHANISM */
