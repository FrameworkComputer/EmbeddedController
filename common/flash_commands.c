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
	uart_printf("Usable flash size: %d KB\n", flash_get_size());
	return EC_SUCCESS;
}


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


static const struct console_command console_commands[] = {
	{"flasherase", command_flash_erase},
	{"flashinfo", command_flash_info},
	{"flashwrite", command_flash_write},
	{"flashwp", command_flash_wp},
};
static const struct console_group command_group = {
	"Flash", console_commands, ARRAY_SIZE(console_commands)
};



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

/*****************************************************************************/
/* Initialization */

int flash_commands_init(void)
{
	/* Register our internal commands */
	console_register_commands(&command_group);
	return EC_SUCCESS;
}

