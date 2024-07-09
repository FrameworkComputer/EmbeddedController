/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* RAM lock control for NPCX */

#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "mpu.h"
#include "mpu_private.h"
#include "ram_lock.h"
#include "registers.h"
#include "util.h"

#define NPCX_RAM_BASE 0x10058000
#define NPCX_RAMLOCK_MAXSIZE 0x80000
#define NPCX_RAM_SECTOR 0x1000
#define NPCX_RAM_BLOCK 0x8000
#define NPCX_RAM_ALIAS_SHIFT 0x10000000

static int ram_lock_update_lock_region(uint8_t region, uint32_t addr,
				       uint8_t lock_bit)
{
	uint32_t offset;

	addr = (region == REGION_DATA_RAM) ? addr - NPCX_RAM_ALIAS_SHIFT : addr;
	offset = addr - NPCX_RAM_BASE;

	if (offset > NPCX_RAMLOCK_MAXSIZE || offset < 0) {
		return -EC_ERROR_INVAL;
	}

	if (region == REGION_DATA_RAM) {
		SET_FIELD(NPCX_RAM_FETCH_LOCK(offset / NPCX_RAM_BLOCK),
			  NPCX_RAM_LK_FIELD, lock_bit);

		/* Enable BusFault trap when fetching lock region */
		SET_BIT(NPCX_RAM_LK_CTL, NPCX_FETCH_BF_EN);
	} else if (region == REGION_STORAGE) {
		SET_FIELD(NPCX_RAM_WRITE_LOCK(offset / NPCX_RAM_BLOCK),
			  NPCX_RAM_LK_FIELD, lock_bit);
	}

	return EC_SUCCESS;
}

/*
 * Configure the possible part of the given region from the base address.
 */
int ram_lock_config_lock_region(uint8_t region, uint32_t addr, uint32_t size)
{
	int sr_idx;
	uint32_t subregion_base, subregion_size;
	uint8_t natural_alignment = alignment_log2(NPCX_RAM_SECTOR);
	uint8_t lock_region = 0;

	/* Check address is aligned */
	if (addr & (NPCX_RAM_SECTOR - 1)) {
		return -EC_ERROR_INVAL;
	}

	/* Check size is aligned */
	if (size & (NPCX_RAM_SECTOR - 1)) {
		return -EC_ERROR_INVAL;
	}

	/*
	 * Depending on the block alignment this can allow us to cover a larger
	 * area. Generate the subregion mask by walking through each, locking if
	 * it is contained in the requested range.
	 */
	natural_alignment += 3;
	subregion_base = align_down_to_bits(addr, natural_alignment);
	subregion_size = 1 << (natural_alignment - 3);

	do {
		for (sr_idx = 0; sr_idx < 8; sr_idx++) {
			if (subregion_base >= addr &&
			    (subregion_base + subregion_size) <= (addr + size))
				lock_region |= 1 << sr_idx;

			subregion_base += subregion_size;
		}

		ram_lock_update_lock_region(
			region, subregion_base - (8 * subregion_size),
			lock_region);
		lock_region = 0;
	} while (subregion_base < (addr + size));

	return EC_SUCCESS;
}
