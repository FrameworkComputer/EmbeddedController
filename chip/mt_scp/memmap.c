/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SCP memory map
 */

#include "common.h"
#include "compile_time_macros.h"
#include "hooks.h"
#include "memmap.h"
#include "registers.h"

/*
 * Map SCP address (bits 31~28) to AP address
 *
 * SCP addr    :  AP addr
 * 0x20000000     0x40000000
 * 0x30000000     0x50000000
 * 0x60000000     0x60000000
 * 0x70000000     0x70000000
 * 0x80000000     0x80000000
 * 0x90000000     0x00000000
 * 0xA0000000     0x10000000
 * 0xB0000000     0x20000000
 * 0xC0000000     0x30000000
 * 0xD0000000     0x10000000
 * 0xE0000000     0xA0000000
 * 0xF0000000     0x90000000
 */

#define MAP_INVALID 0xff

static const uint8_t addr_map[16] = {
	MAP_INVALID,			/* 0x0: SRAM */
	MAP_INVALID,			/* 0x1: Cached access (see below) */
	0x4, 0x5,			/* 0x2-0x3 */
	MAP_INVALID, MAP_INVALID,	/* 0x4-0x5 (unmapped: registers) */
	0x6, 0x7, 0x8,			/* 0x6-0x8 */
	0x0, 0x1, 0x2, 0x3,		/* 0x9-0xc */
	0x1, 0xa, 0x9			/* 0xd-0xf */
};

/*
 * AP addr     :  SCP cache addr
 * 0x50000000     0x10000000
 */
#define CACHE_TRANS_AP_ADDR 0x50000000
#define CACHE_TRANS_SCP_CACHE_ADDR 0x10000000

void scp_memmap_init(void)
{
	/*
	 * Default config, LARGE DRAM not active:
	 *   REG32(0xA0001F00) & 0x2000 != 0
	 */

	/*
	 * SCP_REMAP_CFG1
	 * EXT_ADDR3[29:24] remap register for addr msb 31~28 equal to 0x7
	 * EXT_ADDR2[21:16] remap register for addr msb 31~28 equal to 0x6
	 * EXT_ADDR1[13:8]  remap register for addr msb 31~28 equal to 0x3
	 * EXT_ADDR0[5:0]   remap register for addr msb 31~28 equal to 0x2
	 */
	SCP_REMAP_CFG1 =
		(uint32_t)addr_map[0x7] << 24 |
		(uint32_t)addr_map[0x6] << 16 |
		(uint32_t)addr_map[0x3] << 8 |
		(uint32_t)addr_map[0x2];

	/*
	 * SCP_REMAP_CFG2
	 * EXT_ADDR7[29:24] remap register for addr msb 31~28 equal to 0xb
	 * EXT_ADDR6[21:16] remap register for addr msb 31~28 equal to 0xa
	 * EXT_ADDR5[13:8]  remap register for addr msb 31~28 equal to 0x9
	 * EXT_ADDR4[5:0]   remap register for addr msb 31~28 equal to 0x8
	 */
	SCP_REMAP_CFG2 =
		(uint32_t)addr_map[0xb] << 24 |
		(uint32_t)addr_map[0xa] << 16 |
		(uint32_t)addr_map[0x9] << 8 |
		(uint32_t)addr_map[0x8];
	/*
	 * SCP_REMAP_CFG3
	 * AUD_ADDR[31:28]  remap register for addr msb 31~28 equal to 0xd
	 * EXT_ADDR10[21:16]remap register for addr msb 31~28 equal to 0xf
	 * EXT_ADDR9[13:8]  remap register for addr msb 31~28 equal to 0xe
	 * EXT_ADDR8[5:0]   remap register for addr msb 31~28 equal to 0xc
	 */
	SCP_REMAP_CFG3 =
		(uint32_t)addr_map[0xd] << 28 |
		(uint32_t)addr_map[0xf] << 16 |
		(uint32_t)addr_map[0xe] << 8 |
		(uint32_t)addr_map[0xc];
}

int memmap_ap_to_scp(uintptr_t ap_addr, uintptr_t *scp_addr)
{
	int i;
	uint8_t msb = ap_addr >> SCP_REMAP_ADDR_SHIFT;

	for (i = 0; i < ARRAY_SIZE(addr_map); i++) {
		if (addr_map[i] != msb)
			continue;

		*scp_addr = (ap_addr & SCP_REMAP_ADDR_LSB_MASK) |
			(i << SCP_REMAP_ADDR_SHIFT);
		return EC_SUCCESS;
	}

	return EC_ERROR_INVAL;
}

int memmap_scp_to_ap(uintptr_t scp_addr, uintptr_t *ap_addr)
{
	int i = scp_addr >> SCP_REMAP_ADDR_SHIFT;

	if (addr_map[i] == MAP_INVALID)
		return EC_ERROR_INVAL;

	*ap_addr = (scp_addr & SCP_REMAP_ADDR_LSB_MASK) |
		(addr_map[i] << SCP_REMAP_ADDR_SHIFT);
	return EC_SUCCESS;
}

int memmap_ap_to_scp_cache(uintptr_t ap_addr, uintptr_t *scp_addr)
{
	if ((ap_addr & SCP_L1_EXT_ADDR_OTHER_MSB_MASK) != CACHE_TRANS_AP_ADDR)
		return EC_ERROR_INVAL;

	*scp_addr = CACHE_TRANS_SCP_CACHE_ADDR |
		(ap_addr & SCP_L1_EXT_ADDR_OTHER_LSB_MASK);
	return EC_SUCCESS;
}

int memmap_scp_cache_to_ap(uintptr_t scp_addr, uintptr_t *ap_addr)
{
	if ((scp_addr & SCP_L1_EXT_ADDR_OTHER_MSB_MASK) !=
			CACHE_TRANS_SCP_CACHE_ADDR)
		return EC_ERROR_INVAL;

	*ap_addr = CACHE_TRANS_AP_ADDR |
		(scp_addr & SCP_L1_EXT_ADDR_OTHER_LSB_MASK);
	return EC_SUCCESS;
}
