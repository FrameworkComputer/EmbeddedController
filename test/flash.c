/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Console commands to trigger flash host commands */

#include "board.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "uart.h"
#include "util.h"

static int ro_image_size(int argc, char **argv)
{
	uart_printf("RO image size = 0x%x\n", CONFIG_SECTION_RO_SIZE);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rosize, ro_image_size,
			NULL,
			"Report size of RO image",
			NULL);

static int hc_flash_info(int argc, char **argv)
{
	uint8_t data[EC_PARAM_SIZE];
	enum ec_status res;
	int resp_size;
	struct ec_response_flash_info *r =
		(struct ec_response_flash_info *)data;

	res = host_command_process(0, EC_CMD_FLASH_INFO, data, &resp_size);
	if (res != EC_RES_SUCCESS)
		return EC_ERROR_UNKNOWN;
	uart_printf("flash_size = %d\n", r->flash_size);
	uart_printf("write_block_size = %d\n", r->write_block_size);
	uart_printf("erase_block_size = %d\n", r->erase_block_size);
	uart_printf("protect_block_size = %d\n", r->protect_block_size);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hcflashinfo, hc_flash_info,
			NULL, NULL, NULL);

static int hc_flash_read(int argc, char **argv)
{
	uint8_t data[EC_PARAM_SIZE];
	enum ec_status res;
	int resp_size;
	struct ec_params_flash_read *p =
		(struct ec_params_flash_read *)data;
	struct ec_response_flash_read *r =
		(struct ec_response_flash_read *)data;
	char *e;
	int i, size;

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	p->offset = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;
	size = strtoi(argv[2], &e, 0);
	p->size = size;
	if (*e)
		return EC_ERROR_PARAM2;

	res = host_command_process(0, EC_CMD_FLASH_READ, data, &resp_size);
	if (res != EC_RES_SUCCESS)
		return EC_ERROR_UNKNOWN;
	for (i = 0; i < size; ++i) {
		uart_printf("%02x", r->data[i]);
		if ((i & 31) == 31)
			uart_puts("\n");
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hcflashread, hc_flash_read,
			NULL, NULL, NULL);

static int hc_flash_write(int argc, char **argv)
{
	uint8_t data[EC_PARAM_SIZE];
	enum ec_status res;
	int resp_size;
	struct ec_params_flash_write *p =
		(struct ec_params_flash_write *)data;
	char *e;
	int i, size;
	int seed, mult, add;

	if (argc != 6)
		return EC_ERROR_PARAM_COUNT;

	p->offset = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;
	size = strtoi(argv[2], &e, 0);
	p->size = size;
	if (*e)
		return EC_ERROR_PARAM2;
	seed = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;
	mult = strtoi(argv[4], &e, 0);
	if (*e)
		return EC_ERROR_PARAM4;
	add = strtoi(argv[5], &e, 0);
	if (*e)
		return EC_ERROR_PARAM5;

	for (i = 0; i < size; ++i) {
		p->data[i] = (uint8_t)(seed & 0xff);
		seed = seed * mult + add;
	}

	res = host_command_process(0, EC_CMD_FLASH_WRITE, data, &resp_size);
	if (res != EC_RES_SUCCESS)
		return EC_ERROR_UNKNOWN;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hcflashwrite, hc_flash_write,
			NULL, NULL, NULL);

static int hc_flash_erase(int argc, char **argv)
{
	uint8_t data[EC_PARAM_SIZE];
	enum ec_status res;
	int resp_size;
	struct ec_params_flash_erase *p =
		(struct ec_params_flash_erase *)data;
	char *e;
	int size;

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	p->offset = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;
	size = strtoi(argv[2], &e, 0);
	p->size = size;
	if (*e)
		return EC_ERROR_PARAM2;

	res = host_command_process(0, EC_CMD_FLASH_ERASE, data, &resp_size);
	if (res != EC_RES_SUCCESS)
		return EC_ERROR_UNKNOWN;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hcflasherase, hc_flash_erase,
			NULL, NULL, NULL);
