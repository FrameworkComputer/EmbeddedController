/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * DFU Boot Manager shared utilities for STM32
 */

#include "bkpdata.h"
#include "clock.h"
#include "dfu_bootmanager_shared.h"
#include "flash.h"
#include "registers.h"
#include "task.h"

/*
 * The Servo platforms do not have any free backup regions. The scratchpad
 * is only with the console command scratchpad and on some of the tests so
 * we'll use the scratchpad region.
 */
#ifdef CONFIG_CMD_SCRATCHPAD
#error "The scratchpad is used, define a backup region for the DFU fields."
#endif /* CONFIG_CMD_SCRATCHPAD */

int dfu_bootmanager_enter_dfu(void)
{
	dfu_bootmanager_backup_write(DFU_BOOTMANAGER_VALUE_DFU);
	system_reset(0);
	return 0;
}

void dfu_bootmanager_clear(void)
{
	dfu_bootmanager_backup_write(DFU_BOOTMANAGER_VALUE_CLEAR);
}

void dfu_bootmanager_backup_write(uint8_t value)
{
	uint16_t data = DFU_BOOTMANAGER_VALID_CHECK | value;

	bkpdata_write(BKPDATA_INDEX_SCRATCHPAD, data);
}

int dfu_bootmanager_backup_read(uint8_t *value)
{
	uint16_t data = bkpdata_read(BKPDATA_INDEX_SCRATCHPAD);
	uint16_t valid_check = data & DFU_BOOTMANAGER_VALID_MASK;

	*value = (uint8_t)(data & DFU_BOOTMANAGER_VALUE_MASK);
	if (valid_check != DFU_BOOTMANAGER_VALID_CHECK)
		return EC_ERROR_UNKNOWN;
	return EC_SUCCESS;
}
