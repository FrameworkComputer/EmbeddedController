/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SCP memory map
 */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "hooks.h"
#include "memmap.h"
#include "registers.h"
#include "util.h"

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
/* FIXME: This should be configurable */
#define CACHE_TRANS_AP_SIZE 0x00400000

#ifdef CONFIG_DRAM_BASE
BUILD_ASSERT(CONFIG_DRAM_BASE_LOAD == CACHE_TRANS_AP_ADDR);
BUILD_ASSERT(CONFIG_DRAM_BASE == CACHE_TRANS_SCP_CACHE_ADDR);
#endif

static void cpu_invalidate_icache(void)
{
	SCP_CACHE_OP(CACHE_ICACHE) &= ~SCP_CACHE_OP_OP_MASK;
	SCP_CACHE_OP(CACHE_ICACHE) |=
		OP_INVALIDATE_ALL_LINES | SCP_CACHE_OP_EN;
	asm volatile("dsb; isb");
}

void cpu_invalidate_dcache(void)
{
	SCP_CACHE_OP(CACHE_DCACHE) &= ~SCP_CACHE_OP_OP_MASK;
	SCP_CACHE_OP(CACHE_DCACHE) |=
		OP_INVALIDATE_ALL_LINES | SCP_CACHE_OP_EN;
	/* Read is necessary to confirm the invalidation finish. */
	REG32(CACHE_TRANS_SCP_CACHE_ADDR);
	asm volatile("dsb;");
}

void cpu_invalidate_dcache_range(uintptr_t base, unsigned int length)
{
	size_t pos;
	uintptr_t addr;

	for (pos = 0; pos < length; pos += SCP_CACHE_LINE_SIZE) {
		addr = base + pos;
		SCP_CACHE_OP(CACHE_DCACHE) = addr & SCP_CACHE_OP_TADDR_MASK;
		SCP_CACHE_OP(CACHE_DCACHE) |=
			OP_INVALIDATE_ONE_LINE_BY_ADDRESS | SCP_CACHE_OP_EN;
		/* Read necessary to confirm the invalidation finish. */
		REG32(addr);
	}
	asm volatile("dsb;");
}

void cpu_clean_invalidate_dcache(void)
{
	SCP_CACHE_OP(CACHE_DCACHE) &= ~SCP_CACHE_OP_OP_MASK;
	SCP_CACHE_OP(CACHE_DCACHE) |=
		OP_CACHE_FLUSH_ALL_LINES | SCP_CACHE_OP_EN;
	SCP_CACHE_OP(CACHE_DCACHE) &= ~SCP_CACHE_OP_OP_MASK;
	SCP_CACHE_OP(CACHE_DCACHE) |=
		OP_INVALIDATE_ALL_LINES | SCP_CACHE_OP_EN;
	/* Read necessary to confirm the invalidation finish. */
	REG32(CACHE_TRANS_SCP_CACHE_ADDR);
	asm volatile("dsb;");
}

void cpu_clean_invalidate_dcache_range(uintptr_t base, unsigned int length)
{
	size_t pos;
	uintptr_t addr;

	for (pos = 0; pos < length; pos += SCP_CACHE_LINE_SIZE) {
		addr = base + pos;
		SCP_CACHE_OP(CACHE_DCACHE) = addr & SCP_CACHE_OP_TADDR_MASK;
		SCP_CACHE_OP(CACHE_DCACHE) |=
			OP_CACHE_FLUSH_ONE_LINE_BY_ADDRESS | SCP_CACHE_OP_EN;
		SCP_CACHE_OP(CACHE_DCACHE) = addr & SCP_CACHE_OP_TADDR_MASK;
		SCP_CACHE_OP(CACHE_DCACHE) |=
			OP_INVALIDATE_ONE_LINE_BY_ADDRESS | SCP_CACHE_OP_EN;
		/* Read necessary to confirm the invalidation finish. */
		REG32(addr);
	}
	asm volatile("dsb;");
}

static void scp_cache_init(void)
{
	int c;
	const int region = 0;

	/* First make sure all caches are disabled, and reset stats. */
	for (c = 0; c < CACHE_COUNT; c++) {
		/*
		 * Changing cache-size config may change the SRAM logical
		 * address in the mean time.  This may break the loaded
		 * memory layout, and thus break the system.  Cache-size
		 * should only be be configured in kernel driver before
		 * laoding the firmware. b/137920815#comment18
		 */
		SCP_CACHE_CON(c) &= (SCP_CACHE_CON_CACHESIZE_MASK |
				     SCP_CACHE_CON_WAYEN);
		SCP_CACHE_REGION_EN(c) = 0;
		SCP_CACHE_ENTRY(c, region) = 0;
		SCP_CACHE_END_ENTRY(c, region) = 0;

		/* Reset statistics. */
		SCP_CACHE_HCNT0U(c) = 0;
		SCP_CACHE_HCNT0L(c) = 0;
		SCP_CACHE_CCNT0U(c) = 0;
		SCP_CACHE_CCNT0L(c) = 0;
	}

	/* No "normal" remap. */
	SCP_L1_REMAP_CFG0 = 0;
	SCP_L1_REMAP_CFG1 = 0;
	SCP_L1_REMAP_CFG2 = 0;
	SCP_L1_REMAP_CFG3 = 0;
	/*
	 * Setup OTHER1: Remap register for addr msb 31 to 28 equal to 0x1 and
	 * not overlap with L1C_EXT_ADDR0 to L1C_EXT_ADDR7.
	 */
	SCP_L1_REMAP_OTHER =
		(CACHE_TRANS_AP_ADDR >> SCP_L1_EXT_ADDR_OTHER_SHIFT) << 8;

	/* Disable sleep protect */
	SCP_SLP_PROTECT_CFG = SCP_SLP_PROTECT_CFG &
		~(P_CACHE_SLP_PROT_EN | D_CACHE_SLP_PROT_EN);

	/* Enable region 0 for both I-cache and D-cache. */
	for (c = 0; c < CACHE_COUNT; c++) {
		SCP_CACHE_ENTRY(c, region) = CACHE_TRANS_SCP_CACHE_ADDR;
		SCP_CACHE_END_ENTRY(c, region) =
			CACHE_TRANS_SCP_CACHE_ADDR + CACHE_TRANS_AP_SIZE;
		SCP_CACHE_ENTRY(c, region) |= SCP_CACHE_ENTRY_C;

		SCP_CACHE_REGION_EN(c) |= 1 << region;

		/*
		 * Enable cache. Note that cache size setting should have been
		 * done in kernel driver. b/137920815#comment18
		 */
		SCP_CACHE_CON(c) |= SCP_CACHE_CON_MCEN | SCP_CACHE_CON_CNTEN0;
	}

	cpu_invalidate_icache();
	cpu_invalidate_dcache();
}

static int command_cacheinfo(int argc, char **argv)
{
	const char cache_name[] = {'I', 'D'};
	int c;

	for (c = 0; c < 2; c++) {
		uint64_t hit = ((uint64_t)SCP_CACHE_HCNT0U(c) << 32) |
			SCP_CACHE_HCNT0L(c);
		uint64_t access = ((uint64_t)SCP_CACHE_CCNT0U(c) << 32) |
			SCP_CACHE_CCNT0L(c);

		ccprintf("%ccache hit count:    %llu\n", cache_name[c], hit);
		ccprintf("%ccache access count: %llu\n", cache_name[c], access);
	}

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(cacheinfo, command_cacheinfo,
			     NULL,
			     "Dump cache info");

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

	/* Initialize cache remapping. */
	scp_cache_init();
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

#ifdef CONFIG_DRAM_BASE
BUILD_ASSERT(CONFIG_DRAM_BASE_LOAD == CACHE_TRANS_AP_ADDR);
BUILD_ASSERT(CONFIG_DRAM_BASE == CACHE_TRANS_SCP_CACHE_ADDR);
#endif

int memmap_ap_to_scp_cache(uintptr_t ap_addr, uintptr_t *scp_addr)
{
	uintptr_t lsb;

	if ((ap_addr & SCP_L1_EXT_ADDR_OTHER_MSB_MASK) != CACHE_TRANS_AP_ADDR)
		return EC_ERROR_INVAL;

	lsb = ap_addr & SCP_L1_EXT_ADDR_OTHER_LSB_MASK;
	if (lsb > CACHE_TRANS_AP_SIZE)
		return EC_ERROR_INVAL;

	*scp_addr = CACHE_TRANS_SCP_CACHE_ADDR | lsb;
	return EC_SUCCESS;
}

int memmap_scp_cache_to_ap(uintptr_t scp_addr, uintptr_t *ap_addr)
{
	uintptr_t lsb;

	if ((scp_addr & SCP_L1_EXT_ADDR_OTHER_MSB_MASK) !=
			CACHE_TRANS_SCP_CACHE_ADDR)
		return EC_ERROR_INVAL;

	lsb = scp_addr & SCP_L1_EXT_ADDR_OTHER_LSB_MASK;
	if (lsb >= CACHE_TRANS_AP_SIZE)
		return EC_ERROR_INVAL;

	*ap_addr = CACHE_TRANS_AP_ADDR | lsb;
	return EC_SUCCESS;
}
