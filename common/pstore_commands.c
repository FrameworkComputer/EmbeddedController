/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Persistent storage commands for Chrome EC */

#include "board.h"
#include "eeprom.h"
#include "lpc_commands.h"
#include "pstore_commands.h"
#include "uart.h"
#include "util.h"

/* TODO: move these to board.h */
#ifdef CONFIG_PSTORE
/* Start block and block count for host persistent storage in EC EEPROM */
#define PSTORE_EEPROM_BLOCK_START 16
#define PSTORE_EEPROM_BLOCK_COUNT 16
#endif

enum lpc_status pstore_command_get_info(uint8_t *data)
{
	struct lpc_response_pstore_info *r =
			(struct lpc_response_pstore_info *)data;

	uart_printf("ee block size=%d, count=%d\n",
		    eeprom_get_block_size(), eeprom_get_block_count());

	ASSERT(PSTORE_EEPROM_BLOCK_START + PSTORE_EEPROM_BLOCK_COUNT <=
	       eeprom_get_block_count());

	r->pstore_size = PSTORE_EEPROM_BLOCK_COUNT * eeprom_get_block_size();
	r->access_size = sizeof(uint32_t);
	return EC_LPC_STATUS_SUCCESS;
}


enum lpc_status pstore_command_read(uint8_t *data)
{
	struct lpc_params_pstore_read *p =
			(struct lpc_params_pstore_read *)data;
	struct lpc_response_pstore_read *r =
			(struct lpc_response_pstore_read *)data;
	char *dest = r->data;
	int block_size = eeprom_get_block_size();
	int block = p->offset / block_size + PSTORE_EEPROM_BLOCK_COUNT;
	int offset = p->offset % block_size;
	int bytes_left = p->size;

	if (p->size > sizeof(r->data))
		return EC_LPC_STATUS_ERROR;

	while (bytes_left) {
		/* Read what we can from the current block */
		int bytes_this = MIN(bytes_left, block_size - offset);

		if (block >=
		    PSTORE_EEPROM_BLOCK_START + PSTORE_EEPROM_BLOCK_COUNT)
			return EC_LPC_STATUS_ERROR;

		if (eeprom_read(block, offset, bytes_this, dest))
			return EC_LPC_STATUS_ERROR;

		/* Continue to the next block if necessary */
		offset = 0;
		block++;
		bytes_left -= bytes_this;
		dest += bytes_this;
	}

	return EC_LPC_STATUS_SUCCESS;
}


enum lpc_status pstore_command_write(uint8_t *data)
{
	struct lpc_params_pstore_write *p =
			(struct lpc_params_pstore_write *)data;

	const char *src = p->data;
	int block_size = eeprom_get_block_size();
	int block = p->offset / block_size + PSTORE_EEPROM_BLOCK_COUNT;
	int offset = p->offset % block_size;
	int bytes_left = p->size;

	if (p->size > sizeof(p->data))
		return EC_LPC_STATUS_ERROR;

	while (bytes_left) {
		/* Write what we can to the current block */
		int bytes_this = MIN(bytes_left, block_size - offset);

		if (block >=
		    PSTORE_EEPROM_BLOCK_START + PSTORE_EEPROM_BLOCK_COUNT)
			return EC_LPC_STATUS_ERROR;

		if (eeprom_write(block, offset, bytes_this, src))
			return EC_LPC_STATUS_ERROR;

		/* Continue to the next block if necessary */
		offset = 0;
		block++;
		bytes_left -= bytes_this;
		src += bytes_this;
	}

	return EC_LPC_STATUS_SUCCESS;
}
