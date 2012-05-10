/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Persistent storage commands for Chrome EC */

#include "board.h"
#include "eeprom.h"
#include "host_command.h"
#include "util.h"


int pstore_command_get_info(uint8_t *data, int *resp_size)
{
	struct ec_response_pstore_info *r =
			(struct ec_response_pstore_info *)data;

	ASSERT(EEPROM_BLOCK_START_PSTORE + EEPROM_BLOCK_COUNT_PSTORE <=
	       eeprom_get_block_count());

	r->pstore_size = EEPROM_BLOCK_COUNT_PSTORE * eeprom_get_block_size();
	r->access_size = sizeof(uint32_t);
	*resp_size = sizeof(struct ec_response_pstore_info);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PSTORE_INFO, pstore_command_get_info);


int pstore_command_read(uint8_t *data, int *resp_size)
{
	struct ec_params_pstore_read *p =
			(struct ec_params_pstore_read *)data;
	struct ec_response_pstore_read *r =
			(struct ec_response_pstore_read *)data;
	char *dest = r->data;
	int block_size = eeprom_get_block_size();
	int block = p->offset / block_size + EEPROM_BLOCK_COUNT_PSTORE;
	int offset = p->offset % block_size;
	int bytes_left = p->size;

	if (p->size > sizeof(r->data))
		return EC_RES_ERROR;

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

	*resp_size = sizeof(struct ec_response_pstore_read);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PSTORE_READ, pstore_command_read);


int pstore_command_write(uint8_t *data, int *resp_size)
{
	struct ec_params_pstore_write *p =
			(struct ec_params_pstore_write *)data;

	const char *src = p->data;
	int block_size = eeprom_get_block_size();
	int block = p->offset / block_size + EEPROM_BLOCK_COUNT_PSTORE;
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
DECLARE_HOST_COMMAND(EC_CMD_PSTORE_WRITE, pstore_command_write);
