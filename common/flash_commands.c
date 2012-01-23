/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "console.h"
#include "flash.h"
#include "flash_commands.h"
#include "lpc_commands.h"
#include "registers.h"  /* TODO: remove; only for temp debugging */
#include "shared_mem.h"
#include "uart.h"
#include "util.h"


/*****************************************************************************/
/* Console commands */

static int command_flash_info(int argc, char **argv)
{
	uart_printf("Usable flash size: %d B\n", flash_get_size());
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flashinfo, command_flash_info);


static int command_flash_erase(int argc, char **argv)
{
	int offset = 0;
	int size = FLASH_ERASE_BYTES;
	char *endptr;
	int rv;

	if (argc < 2) {
		uart_puts("Usage: flasherase <offset> [size]\n");
		return EC_ERROR_UNKNOWN;
	}

	offset = strtoi(argv[1], &endptr, 0);
	if (*endptr) {
		uart_puts("Invalid offset\n");
		return EC_ERROR_UNKNOWN;
	}

	if (argc > 2) {
		size = strtoi(argv[2], &endptr, 0);
		if (*endptr) {
			uart_puts("Invalid size\n");
			return EC_ERROR_UNKNOWN;
		}
	}

	uart_printf("Erasing %d bytes at offset 0x%x (%d)...\n",
		    size, offset, offset);
	rv = flash_erase(offset, size);
	if (rv == EC_SUCCESS)
		uart_puts("done.\n");
	else
		uart_printf("failed. (error %d)\n", rv);

	return rv;
}
DECLARE_CONSOLE_COMMAND(flasherase, command_flash_erase);


static int command_flash_write(int argc, char **argv)
{
	char *data;
	int offset = 0;
	int size = 1024;  /* Default size */
	char *endptr;
	int rv;
	int i;

	if (argc < 2) {
		uart_puts("Usage: flashwrite <offset> <size>\n");
		return EC_ERROR_UNKNOWN;
	}

	offset = strtoi(argv[1], &endptr, 0);
	if (*endptr) {
		uart_puts("Invalid offset\n");
		return EC_ERROR_UNKNOWN;
	}

	if (argc > 2) {
		size = strtoi(argv[2], &endptr, 0);
		if (*endptr) {
			uart_puts("Invalid size\n");
			return EC_ERROR_UNKNOWN;
		}
		if (size > shared_mem_size()) {
			uart_puts("Truncating size\n");
			size = sizeof(data);
		}
	}

        /* Acquire the shared memory buffer */
	rv = shared_mem_acquire(size, 0, &data);
	if (rv) {
		uart_printf("Unable to acquire %d byte buffer\n", size);
		return rv;
	}

	/* Fill the data buffer with a pattern */
	for (i = 0; i < size; i++)
		data[i] = i;

	uart_printf("Writing %d bytes to offset 0x%x (%d)...\n",
		    size, offset, offset);
	rv = flash_write(offset, size, data);
	if (rv == EC_SUCCESS)
		uart_puts("done.\n");
	else
		uart_printf("failed. (error %d)\n", rv);

	/* Free the buffer */
	shared_mem_release(data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(flashwrite, command_flash_write);


static int command_flash_wp(int argc, char **argv)
{
	int b = 0;
	char *endptr;

	if (argc < 2) {
		uart_puts("Usage: flashwp [bitmask]\n");
		uart_printf("(current value of FMPPE1: 0x%08x)\n",
			    LM4_FLASH_FMPPE1);
		return EC_SUCCESS;
	}

	b = strtoi(argv[1], &endptr, 0);
	if (*endptr) {
		uart_puts("Invalid bitmask\n");
		return EC_ERROR_UNKNOWN;
	}

	uart_printf("FMPPE1 before: 0x%08x\n", LM4_FLASH_FMPPE1);
	LM4_FLASH_FMPPE1 = b;
	uart_printf("FMPPE1 after: 0x%08x\n", LM4_FLASH_FMPPE1);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flashwp, command_flash_wp);

static int command_flash_wp_range(int argc, char **argv)
{
	int offset, size;
	char *endptr;
	int rv;

	if (argc < 3) {
		uart_puts("Usage: flashwprange [offset size]\n");
		rv = flash_get_write_protect_range(&offset, &size);
		if (rv)
			uart_puts("flash_get_write_protect_range failed\n");
		else
			uart_printf("Current range : offset(%d) size(%d)\n",
					offset, size);
		uart_printf("FMPPEs : %08x %08x %08x %08x\n",
				LM4_FLASH_FMPPE0, LM4_FLASH_FMPPE1,
				LM4_FLASH_FMPPE2, LM4_FLASH_FMPPE3);
	} else {
		offset = strtoi(argv[1], &endptr, 0);
		if (*endptr) {
			uart_printf("Invalid offset \"%s\"\n", argv[1]);
			return EC_ERROR_UNKNOWN;
		}
		size = strtoi(argv[2], &endptr, 0);
		if (*endptr) {
			uart_printf("Invalid size \"%s\"\n", argv[2]);
			return EC_ERROR_UNKNOWN;
		}

		rv = flash_set_write_protect_range(offset, size);
		if (rv) {
			uart_puts("flash_set_write_protect_range failed\n");
			return rv;
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flashwprange, command_flash_wp_range);


/*****************************************************************************/
/* Host commands */

enum lpc_status flash_command_get_info(uint8_t *data)
{
	struct lpc_response_flash_info *r =
			(struct lpc_response_flash_info *)data;

	r->flash_size = flash_get_size();
	r->write_block_size = FLASH_WRITE_BYTES;
	r->erase_block_size = FLASH_ERASE_BYTES;
	r->protect_block_size = FLASH_PROTECT_BYTES;
	return EC_LPC_STATUS_SUCCESS;
}


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
		if (flash_read(p->offset + j, 1, &byte)) {
			uart_printf("flash_read() error at 0x%02x.\n",
			            p->offset + j);
			return EC_LPC_STATUS_ERROR;
		}
		BYTE_IN(cs, byte);
	}

	r->checksum = cs;

	return EC_LPC_STATUS_SUCCESS;
}
#endif


enum lpc_status flash_command_read(uint8_t *data)
{
	struct lpc_params_flash_read *p =
			(struct lpc_params_flash_read *)data;
	struct lpc_response_flash_read *r =
			(struct lpc_response_flash_read *)data;

	if (p->size > sizeof(r->data))
		return EC_LPC_STATUS_ERROR;

	if (flash_read(p->offset, p->size, r->data))
		return EC_LPC_STATUS_ERROR;

	return EC_LPC_STATUS_SUCCESS;
}


enum lpc_status flash_command_write(uint8_t *data)
{
	struct lpc_params_flash_write *p =
			(struct lpc_params_flash_write *)data;

	if (p->size > sizeof(p->data))
		return EC_LPC_STATUS_ERROR;

	if (flash_write(p->offset, p->size, p->data))
		return EC_LPC_STATUS_ERROR;

	return EC_LPC_STATUS_SUCCESS;
}


enum lpc_status flash_command_erase(uint8_t *data)
{
	struct lpc_params_flash_erase *p =
			(struct lpc_params_flash_erase *)data;

	if (flash_erase(p->offset, p->size))
		return EC_LPC_STATUS_ERROR;

	return EC_LPC_STATUS_SUCCESS;
}

/* TODO: use shadow range in EEPROM */
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
		return EC_LPC_STATUS_ERROR;

	return EC_LPC_STATUS_SUCCESS;
}

enum lpc_status flash_command_wp_get_state(uint8_t *data)
{
	struct lpc_response_flash_wp_enable *p =
			(struct lpc_response_flash_wp_enable *)data;

	if (flash_get_write_protect_status() & EC_FLASH_WP_RANGE_LOCKED)
		p->enable_wp = 1;
	else
		p->enable_wp = 0;

	return EC_LPC_STATUS_SUCCESS;
}

enum lpc_status flash_command_wp_set_range(uint8_t *data)
{
	struct lpc_params_flash_wp_range *p =
			(struct lpc_params_flash_wp_range *)data;

	if (flash_set_write_protect_range(p->offset, p->size))
		return EC_LPC_STATUS_ERROR;

	return EC_LPC_STATUS_SUCCESS;
}

enum lpc_status flash_command_wp_get_range(uint8_t *data)
{
	struct lpc_response_flash_wp_range *p =
			(struct lpc_response_flash_wp_range *)data;

	if (flash_get_write_protect_range(&p->offset, &p->size))
		return EC_LPC_STATUS_ERROR;

	return EC_LPC_STATUS_SUCCESS;
}
