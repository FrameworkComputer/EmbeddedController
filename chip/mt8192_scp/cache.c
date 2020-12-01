/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cache.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "csr.h"
#include "util.h"

/* rs1 0~31 register X0~X31 */
#define COP(rs1) (((rs1) << 15) | 0x400f)

#define COP_OP_BARRIER_ICACHE		0x0
#define COP_OP_INVALIDATE_ICACHE	0x8
#define COP_OP_INVALIDATE_ICACHE_ADDR	0x9

#define COP_OP_BARRIER_DCACHE		0x10
#define COP_OP_WRITEBACK_DCACHE		0x14
#define COP_OP_WRITEBACK_DCACHE_ADDR	0x15
#define COP_OP_INVALIDATE_DCACHE	0x18
#define COP_OP_INVALIDATE_DCACHE_ADDR	0x19
/* FLUSH = WRITEBACK + INVALIDATE */
#define COP_OP_FLUSH_DCACHE		0x1C
#define COP_OP_FLUSH_DCACHE_ADDR	0x1D

inline static void cache_op_all(uint32_t op)
{
	register int t0 asm("t0") = op;
	asm volatile (".word "STRINGIFY(COP(5)) :: "r"(t0));
}

static int cache_op_addr(uintptr_t addr, uint32_t length, uint32_t op)
{
	size_t offset;
	register int t0 asm("t0");

	/* NOTE: cache operations must use 32 byte aligned address */
	if (addr & GENMASK(3, 0))
		return EC_ERROR_INVAL;

	for (offset = 0; offset < length; offset += 4) {
		t0 = addr + offset + op;
		asm volatile (".word "STRINGIFY(COP(5)) :: "r"(t0));
	}

	return EC_SUCCESS;
}

void cache_barrier_icache(void)
{
	cache_op_all(COP_OP_BARRIER_ICACHE);
}

void cache_invalidate_icache(void)
{
	cache_op_all(COP_OP_INVALIDATE_ICACHE);
}

int cache_invalidate_icache_range(uintptr_t addr, uint32_t length)
{
	return cache_op_addr(addr, length, COP_OP_INVALIDATE_ICACHE_ADDR);
}

void cache_barrier_dcache(void)
{
	cache_op_all(COP_OP_BARRIER_DCACHE);
}

void cache_writeback_dcache(void)
{
	cache_op_all(COP_OP_WRITEBACK_DCACHE);
}

int cache_writeback_dcache_range(uintptr_t addr, uint32_t length)
{
	return cache_op_addr(addr, length, COP_OP_WRITEBACK_DCACHE_ADDR);
}

void cache_invalidate_dcache(void)
{
	cache_op_all(COP_OP_INVALIDATE_DCACHE);
}

int cache_invalidate_dcache_range(uintptr_t addr, uint32_t length)
{
	return cache_op_addr(addr, length, COP_OP_INVALIDATE_DCACHE_ADDR);
}

void cache_flush_dcache(void)
{
	cache_op_all(COP_OP_FLUSH_DCACHE);
}

int cache_flush_dcache_range(uintptr_t addr, uint32_t length)
{
	return cache_op_addr(addr, length, COP_OP_FLUSH_DCACHE_ADDR);
}

extern struct mpu_entry mpu_entries[];

void cache_init(void)
{
	int i;
	uint32_t mpu_en = 0;

	/* disable mpu */
	clear_csr(CSR_MCTREN, CSR_MCTREN_MPU);

	/* enable i$, d$ */
	set_csr(CSR_MCTREN, CSR_MCTREN_ICACHE);
	set_csr(CSR_MCTREN, CSR_MCTREN_DCACHE);

	/* invalidate icache and dcache */
	cache_invalidate_icache();
	cache_invalidate_dcache();

	/* set mpu entries
	 *
	 * The pragma is for force GCC unrolls the following loop.
	 * See b/172886808
	 */
#pragma GCC unroll 16
	for (i = 0; i < NR_MPU_ENTRIES; ++i) {
		if (mpu_entries[i].end_addr - mpu_entries[i].start_addr) {
			write_csr(CSR_MPU_L(i), mpu_entries[i].start_addr |
						mpu_entries[i].attribute);
			write_csr(CSR_MPU_H(i), mpu_entries[i].end_addr);
			mpu_en |= BIT(i);
		}
	}

	/* enable mpu entries */
	write_csr(CSR_MPU_ENTRY_EN, mpu_en);

	/* enable mpu */
	set_csr(CSR_MCTREN, CSR_MCTREN_MPU);

	/* fence */
	asm volatile ("fence.i" ::: "memory");
}

#ifdef DEBUG
/*
 * I for I-cache
 * D for D-cache
 * C for control transfer instructions (branch, jump, ret, interrupt, ...)
 */
static enum {
	PMU_SELECT_I = 0,
	PMU_SELECT_D,
	PMU_SELECT_C
} pmu_select;

int command_enable_pmu(int argc, char **argv)
{
	static const char * const selectors[] = {
		[PMU_SELECT_I] = "I",
		[PMU_SELECT_D] = "D",
		[PMU_SELECT_C] = "C",
	};
	int i;

	if (argc != 2)
		return EC_ERROR_PARAM1;

	for (i = 0; i < ARRAY_SIZE(selectors); ++i) {
		if (strcasecmp(argv[1], selectors[i]) == 0) {
			pmu_select = i;
			break;
		}
	}
	if (i >= ARRAY_SIZE(selectors))
		return EC_ERROR_PARAM1;

	ccprintf("select \"%s\"\n", selectors[pmu_select]);

	/* disable all PMU */
	clear_csr(CSR_PMU_MPMUCTR,
		  CSR_PMU_MPMUCTR_C | CSR_PMU_MPMUCTR_I |
		  CSR_PMU_MPMUCTR_H3 | CSR_PMU_MPMUCTR_H4 |
		  CSR_PMU_MPMUCTR_H5);

	/* reset cycle count */
	write_csr(CSR_PMU_MCYCLE, 0);
	write_csr(CSR_PMU_MCYCLEH, 0);
	/* reset retired-instruction count */
	write_csr(CSR_PMU_MINSTRET, 0);
	write_csr(CSR_PMU_MINSTRETH, 0);
	/* reset counter{3,4,5} */
	write_csr(CSR_PMU_MHPMCOUNTER3, 0);
	write_csr(CSR_PMU_MHPMCOUNTER3H, 0);
	write_csr(CSR_PMU_MHPMCOUNTER4, 0);
	write_csr(CSR_PMU_MHPMCOUNTER4H, 0);
	write_csr(CSR_PMU_MHPMCOUNTER5, 0);
	write_csr(CSR_PMU_MHPMCOUNTER5H, 0);

	/* select different event IDs for counter{3,4,5} */
	switch (pmu_select) {
	case PMU_SELECT_I:
		/* I-cache access count */
		write_csr(CSR_PMU_MHPMEVENT3, 1);
		/* I-cache miss count */
		write_csr(CSR_PMU_MHPMEVENT4, 3);
		/* noncacheable I-AXI access count */
		write_csr(CSR_PMU_MHPMEVENT5, 5);
		break;
	case PMU_SELECT_D:
		/* D-cache access count */
		write_csr(CSR_PMU_MHPMEVENT3, 11);
		/* D-cache miss count */
		write_csr(CSR_PMU_MHPMEVENT4, 12);
		/* noncacheable D-AXI access count */
		write_csr(CSR_PMU_MHPMEVENT5, 14);
		break;
	case PMU_SELECT_C:
		/* control transfer instruction count */
		write_csr(CSR_PMU_MHPMEVENT3, 27);
		/* control transfer miss-predict count */
		write_csr(CSR_PMU_MHPMEVENT4, 28);
		/* interrupt count */
		write_csr(CSR_PMU_MHPMEVENT5, 29);
		break;
	}

	cache_invalidate_icache();
	cache_flush_dcache();

	/* enable all PMU */
	set_csr(CSR_PMU_MPMUCTR,
		CSR_PMU_MPMUCTR_C | CSR_PMU_MPMUCTR_I |
		CSR_PMU_MPMUCTR_H3 | CSR_PMU_MPMUCTR_H4 |
		CSR_PMU_MPMUCTR_H5);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(enable_pmu, command_enable_pmu,
			     "[I | D | C]", "Enable PMU");

int command_disable_pmu(int argc, char **argv)
{
	clear_csr(CSR_PMU_MPMUCTR,
		  CSR_PMU_MPMUCTR_C | CSR_PMU_MPMUCTR_I |
		  CSR_PMU_MPMUCTR_H3 | CSR_PMU_MPMUCTR_H4 |
		  CSR_PMU_MPMUCTR_H5);
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(disable_pmu, command_disable_pmu,
			     NULL, "Disable PMU");

int command_show_pmu(int argc, char **argv)
{
	uint64_t val3, val4, val5;
	uint32_t p;

	val3 = ((uint64_t)read_csr(CSR_PMU_MCYCLEH) << 32) |
			read_csr(CSR_PMU_MCYCLE);
	ccprintf("cycles: %lld\n", val3);

	val3 = ((uint64_t)read_csr(CSR_PMU_MINSTRETH) << 32) |
			read_csr(CSR_PMU_MINSTRET);
	ccprintf("retired instructions: %lld\n", val3);

	val3 = ((uint64_t)read_csr(CSR_PMU_MHPMCOUNTER3H) << 32) |
			read_csr(CSR_PMU_MHPMCOUNTER3);
	val4 = ((uint64_t)read_csr(CSR_PMU_MHPMCOUNTER4H) << 32) |
			read_csr(CSR_PMU_MHPMCOUNTER4);
	val5 = ((uint64_t)read_csr(CSR_PMU_MHPMCOUNTER5H) << 32) |
			read_csr(CSR_PMU_MHPMCOUNTER5);

	if (val3)
		p = val4 * 10000 / val3;
	else
		p = 0;

	switch (pmu_select) {
	case PMU_SELECT_I:
		ccprintf("I-cache:\n");
		ccprintf("  access: %lld\n", val3);
		ccprintf("  miss: %lld (%d.%d%%)\n", val4, p / 100, p % 100);
		ccprintf("non-cacheable I: %lld\n", val5);
		break;
	case PMU_SELECT_D:
		ccprintf("D-cache:\n");
		ccprintf("  access: %lld\n", val3);
		ccprintf("  miss: %lld (%d.%d%%)\n", val4, p / 100, p % 100);
		ccprintf("non-cacheable D: %lld\n", val5);
		break;
	case PMU_SELECT_C:
		ccprintf("control transfer instruction:\n");
		ccprintf("  total: %lld\n", val3);
		ccprintf("  miss-predict: %lld (%d.%d%%)\n",
			 val4, p / 100, p % 100);
		ccprintf("interrupts: %lld\n", val5);
		break;
	}

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(show_pmu, command_show_pmu, NULL, "Show PMU");
#endif
