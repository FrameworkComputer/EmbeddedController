/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cache.h"
#include "registers.h"
#include "stdint.h"

/*
 * Map SCP address (bits 31~28) to AP address
 *
 * SCP address	AP address	Note
 *
 * 0x0000_0000			SRAM
 * 0x1000_0000	0x5000_0000	CPU DRAM
 * 0x2000_0000	0x7000_0000
 * 0x3000_0000
 *
 * 0x4000_0000
 * 0x5000_0000	0x0000_0000
 * 0x6000_0000	0x1000_0000
 * 0x7000_0000	0xa000_0000
 *
 * 0x8000_0000
 * 0x9000_0000	0x8000_0000
 * 0xa000_0000	0x9000_0000
 * 0xb000_0000
 *
 * 0xc000_0000	0x8000_0000
 * 0xd000_0000	0x2000_0000
 * 0xe000_0000	0x3000_0000
 * 0xf000_0000	0x6000_0000
 */

#define REMAP_ADDR_SHIFT 28
#define REMAP_ADDR_LSB_MASK (BIT(REMAP_ADDR_SHIFT) - 1)
#define REMAP_ADDR_MSB_MASK ((~0) << REMAP_ADDR_SHIFT)
#define MAP_INVALID 0xff

#ifdef CHIP_VARIANT_MT8188
static const uint8_t addr_map[16] = {
	MAP_INVALID, /* SRAM */
	0x1, /* ext_addr_0x1 */
	MAP_INVALID, /* ext_addr_0x2 */
	MAP_INVALID, /* ext_addr_0x3 */

	0x4, /* ext_addr_0x4 */
	0x5, /* ext_addr_0x5 */
	0x6, /* ext_addr_0x6 */
	0x7, /* ext_addr_0x7 */

	0x8, /* ext_addr_0x8 */
	0x9, /* ext_addr_0x9 */
	0xa, /* ext_addr_0xa */
	0xb, /* ext_addr_0xb */

	0xc, /* ext_addr_0xc */
	0xd, /* ext_addr_0xd */
	0xe, /* ext_addr_0xe */
	0xf, /* ext_addr_0xf */
};
#else
static const uint8_t addr_map[16] = {
	MAP_INVALID, /* SRAM */
	0x5, /* ext_addr_0x1 */
	0x7, /* ext_addr_0x2 */
	MAP_INVALID, /* no ext_addr_0x3 */

	MAP_INVALID, /* no ext_addr_0x4 */
	0x0, /* ext_addr_0x5 */
	0x1, /* ext_addr_0x6 */
	0xa, /* ext_addr_0x7 */

	MAP_INVALID, /* no ext_addr_0x8 */
	0x8, /* ext_addr_0x9 */
	0x9, /* ext_addr_0xa */
	MAP_INVALID, /* no ext_addr_0xb */

	0x8, /* ext_addr_0xc */
	0x2, /* ext_addr_0xd */
	0x3, /* ext_addr_0xe */
	0x6, /* ext_addr_0xf */
};
#endif

void memmap_init(void)
{
#ifdef CHIP_VARIANT_MT8188
	SCP_R_REMAP_0X4567 =
		(uint32_t)addr_map[0x4] | (uint32_t)addr_map[0x5] << 8 |
		(uint32_t)addr_map[0x6] << 16 | (uint32_t)addr_map[0x7] << 24;

	SCP_R_REMAP_0X89AB =
		(uint32_t)addr_map[0x8] | (uint32_t)addr_map[0x9] << 8 |
		(uint32_t)addr_map[0xa] << 16 | (uint32_t)addr_map[0xb] << 24;

#else
	SCP_R_REMAP_0X0123 = (uint32_t)addr_map[0x1] << 8 |
			     (uint32_t)addr_map[0x2] << 16;

	SCP_R_REMAP_0X4567 = (uint32_t)addr_map[0x5] << 8 |
			     (uint32_t)addr_map[0x6] << 16 |
			     (uint32_t)addr_map[0x7] << 24;

	SCP_R_REMAP_0X89AB = (uint32_t)addr_map[0x9] << 8 |
			     (uint32_t)addr_map[0xa] << 16;
#endif

	SCP_R_REMAP_0XCDEF =
		(uint32_t)addr_map[0xc] | (uint32_t)addr_map[0xd] << 8 |
		(uint32_t)addr_map[0xe] << 16 | (uint32_t)addr_map[0xf] << 24;

	cache_init();
}

int memmap_ap_to_scp(uintptr_t ap_addr, uintptr_t *scp_addr)
{
	int i;
	uint8_t msb = ap_addr >> REMAP_ADDR_SHIFT;

	for (i = 0; i < ARRAY_SIZE(addr_map); ++i) {
		if (addr_map[i] != msb)
			continue;

		*scp_addr = (ap_addr & REMAP_ADDR_LSB_MASK) |
			    (i << REMAP_ADDR_SHIFT);
		return EC_SUCCESS;
	}

	return EC_ERROR_INVAL;
}

int memmap_scp_to_ap(uintptr_t scp_addr, uintptr_t *ap_addr)
{
	int i = scp_addr >> REMAP_ADDR_SHIFT;

	if (addr_map[i] == MAP_INVALID)
		return EC_ERROR_INVAL;

	*ap_addr = (scp_addr & REMAP_ADDR_LSB_MASK) |
		   (addr_map[i] << REMAP_ADDR_SHIFT);
	return EC_SUCCESS;
}
