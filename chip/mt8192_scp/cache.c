/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cache.h"
#include "common.h"
#include "compile_time_macros.h"
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
