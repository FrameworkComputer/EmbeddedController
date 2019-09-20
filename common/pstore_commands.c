/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Persistent storage commands for Chrome EC */

#include "common.h"
#include "eeprom.h"
#include "host_command.h"
#include "util.h"

enum ec_status pstore_command_get_info(struct host_cmd_handler_args *args)
{
	struct ec_response_pstore_info *r = args->response;

	ASSERT(EEPROM_BLOCK_START_PSTORE + EEPROM_BLOCK_COUNT_PSTORE <=
	       eeprom_get_block_count());

	r->pstore_size = EEPROM_BLOCK_COUNT_PSTORE * eeprom_get_block_size();
	r->access_size = sizeof(uint32_t);
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PSTORE_INFO,
		     pstore_command_get_info,
		     EC_VER_MASK(0));

enum ec_status pstore_command_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_pstore_read *p = args->params;
	char *dest = args->response;
	int block_size = eeprom_get_block_size();
	int block = p->offset / block_size + EEPROM_BLOCK_START_PSTORE;
	int offset = p->offset % block_size;
	int bytes_left = p->size;

	if (p->size > args->response_max)
		return EC_RES_INVALID_PARAM;

	while (bytes_left) {
		/* Read what we can from the current block */
		int bytes_this = MIN(bytes_left, block_size - offset);

		if (block >=
		    EEPROM_BLOCK_START_PSTORE + EEPROM_BLOCK_COUNT_PSTORE)
			return EC_RES_ERROR;

		if (eeprom_read(block, offset, bytes_this, dest))
			return EC_RES_ERROR;

		/* Continue to the next block if necessary */
		offset = 0;
		block++;
		bytes_left -= bytes_this;
		dest += bytes_this;
	}

	args->response_size = p->size;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PSTORE_READ,
		     pstore_command_read,
		     EC_VER_MASK(0));

enum ec_status pstore_command_write(struct host_cmd_handler_args *args)
{
	const struct ec_params_pstore_write *p = args->params;

	const char *src = p->data;
	int block_size = eeprom_get_block_size();
	int block = p->offset / block_size + EEPROM_BLOCK_START_PSTORE;
	int offset = p->offset % block_size;
	int bytes_left = p->size;

	if (p->size > sizeof(p->data))
		return EC_RES_ERROR;

	while (bytes_left) {
		/* Write what we can to the current block */
		int bytes_this = MIN(bytes_left, block_size - offset);

		if (block >=
		    EEPROM_BLOCK_START_PSTORE + EEPROM_BLOCK_COUNT_PSTORE)
			return EC_RES_ERROR;

		if (eeprom_write(block, offset, bytes_this, src))
			return EC_RES_ERROR;

		/* Continue to the next block if necessary */
		offset = 0;
		block++;
		bytes_left -= bytes_this;
		src += bytes_this;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PSTORE_WRITE,
		     pstore_command_write,
		     EC_VER_MASK(0));
