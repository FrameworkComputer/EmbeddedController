/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock flash memory module for Chrome EC.
 * Due to RAM size limit, we cannot mock entire flash memory. Instead, we only
 * mock the last bank in order to make pstate works. */

#include "flash.h"
#include "uart.h"
#include "util.h"

#define FLASH_FSIZE         0x7f
#define PHYSICAL_SIZE ((FLASH_FSIZE + 1) * CONFIG_FLASH_BANK_SIZE)
#define FLASH_MOCK_BEGIN (FLASH_FSIZE * CONFIG_FLASH_BANK_SIZE)

char mock_protect[FLASH_FSIZE + 1];
char pstate_space[CONFIG_FLASH_BANK_SIZE];

int flash_physical_size(void)
{
	return PHYSICAL_SIZE;
}


int flash_physical_write(int offset, int size, const char* data)
{
	int i;
	int xorsum = 0;
	if (offset >= FLASH_MOCK_BEGIN)
		memcpy(pstate_space + offset - FLASH_MOCK_BEGIN, data, size);
	else {
		xorsum = 0;
		for (i = 0; i < size; ++i)
			xorsum ^= data[i];
		uart_printf("Flash write at %x size %x XOR %x\n",
			    offset,
			    size,
			    xorsum);
	}
	return EC_SUCCESS;
}


int flash_physical_erase(int offset, int size)
{
	uart_printf("Flash erase at %x size %x\n", offset, size);
	if (offset + size >= FLASH_MOCK_BEGIN)
		memset(pstate_space, 0xff, offset + size - FLASH_MOCK_BEGIN);
	return EC_SUCCESS;
}


int flash_physical_get_protect(int block)
{
	return mock_protect[block];
}

uint32_t flash_get_protect(void)
{
	return 0;
}

int flash_set_protect(uint32_t mask, uint32_t flags)
{
	return 0;
}

int flash_pre_init(void)
{
	return EC_SUCCESS;
}
