/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>

#include "bkpdata.h"
#include "registers.h"
#include "system.h" /* enum system_bbram_idx */
#include "task.h"

uint16_t bkpdata_read(enum bkpdata_index index)
{
	if (index < 0 || index >= STM32_BKP_ENTRIES)
		return 0;

	if (index & 1)
		return STM32_BKP_DATA(index >> 1) >> 16;
	else
		return STM32_BKP_DATA(index >> 1) & 0xFFFF;
}

int bkpdata_write(enum bkpdata_index index, uint16_t value)
{
	static struct mutex bkpdata_write_mutex;
	int use_mutex = !in_interrupt_context();

	if (index < 0 || index >= STM32_BKP_ENTRIES)
		return EC_ERROR_INVAL;

	/*
	 * Two entries share a single 32-bit register, lock mutex to prevent
	 * read/mask/write races.
	 */
	if (use_mutex)
		mutex_lock(&bkpdata_write_mutex);
	if (index & 1) {
		uint32_t val = STM32_BKP_DATA(index >> 1);
		val = (val & 0x0000FFFF) | (value << 16);
		STM32_BKP_DATA(index >> 1) = val;
	} else {
		uint32_t val = STM32_BKP_DATA(index >> 1);
		val = (val & 0xFFFF0000) | value;
		STM32_BKP_DATA(index >> 1) = val;
	}
	if (use_mutex)
		mutex_unlock(&bkpdata_write_mutex);

	return EC_SUCCESS;
}

int bkpdata_index_lookup(enum system_bbram_idx idx, int *msb)
{
	*msb = 0;

#ifdef CONFIG_USB_PD_DUAL_ROLE
	if (idx == SYSTEM_BBRAM_IDX_PD0)
		return BKPDATA_INDEX_PD0;
	if (idx == SYSTEM_BBRAM_IDX_PD1)
		return BKPDATA_INDEX_PD1;
	if (idx == SYSTEM_BBRAM_IDX_PD2)
		return BKPDATA_INDEX_PD2;
#endif
	return -1;
}

uint32_t bkpdata_read_reset_flags()
{
	uint32_t flags = bkpdata_read(BKPDATA_INDEX_SAVED_RESET_FLAGS);

#ifdef CONFIG_STM32_EXTENDED_RESET_FLAGS
	flags |= bkpdata_read(BKPDATA_INDEX_SAVED_RESET_FLAGS_2) << 16;
#endif

	return flags;
}

__overridable
void bkpdata_write_reset_flags(uint32_t save_flags)
{
	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS, save_flags & 0xffff);
#ifdef CONFIG_STM32_EXTENDED_RESET_FLAGS
	bkpdata_write(BKPDATA_INDEX_SAVED_RESET_FLAGS_2, save_flags >> 16);
#endif
}
