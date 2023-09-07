/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CACHE_H
#define __CROS_EC_CACHE_H

#include "common.h"
#include "csr.h"
#include "stdint.h"
#include "util.h"

/* rs1 0~31 register X0~X31 */
#define COP(rs1) (((rs1) << 15) | 0x400f)

#define COP_OP_BARRIER_ICACHE 0x0
#define COP_OP_INVALIDATE_ICACHE 0x8
#define COP_OP_INVALIDATE_ICACHE_ADDR 0x9

#define COP_OP_BARRIER_DCACHE 0x10
#define COP_OP_WRITEBACK_DCACHE 0x14
#define COP_OP_WRITEBACK_DCACHE_ADDR 0x15
#define COP_OP_INVALIDATE_DCACHE 0x18
#define COP_OP_INVALIDATE_DCACHE_ADDR 0x19
/* FLUSH = WRITEBACK + INVALIDATE */
#define COP_OP_FLUSH_DCACHE 0x1C
#define COP_OP_FLUSH_DCACHE_ADDR 0x1D

#ifdef CHIP_VARIANT_MT8188
#define CACHE_LINE_SIZE (128)
#else
#define CACHE_LINE_SIZE (32)
#endif

static inline void cache_op_all(uint32_t op)
{
	register int t0 asm("t0") = op;
	asm volatile(".word " STRINGIFY(COP(5))::"r"(t0));
}

static inline int cache_op_addr(uintptr_t addr, uint32_t length, uint32_t op)
{
	size_t offset;
	register int t0 asm("t0");

	/* NOTE: cache operations must use 32 byte aligned address */
	if (addr & GENMASK(3, 0))
		return EC_ERROR_INVAL;

	for (offset = 0; offset < length; offset += CACHE_LINE_SIZE) {
		t0 = addr + offset + op;
		asm volatile(".word " STRINGIFY(COP(5))::"r"(t0));
	}

	return EC_SUCCESS;
}

/* memory barrier of I$ */
static inline void cache_barrier_icache(void)
{
	cache_op_all(COP_OP_BARRIER_ICACHE);
}

/* invalidate all I$ */
static inline void cache_invalidate_icache(void)
{
	cache_op_all(COP_OP_INVALIDATE_ICACHE);
}

/* invalidate a range of I$ */
static inline int cache_invalidate_icache_range(uintptr_t addr, uint32_t length)
{
	return cache_op_addr(addr, length, COP_OP_INVALIDATE_ICACHE_ADDR);
}

/* memory barrier of D$ */
static inline void cache_barrier_dcache(void)
{
	cache_op_all(COP_OP_BARRIER_DCACHE);
}

/* writeback all D$ */
static inline void cache_writeback_dcache(void)
{
	cache_op_all(COP_OP_WRITEBACK_DCACHE);
	cache_barrier_icache();
	cache_barrier_dcache();
}

/* writeback a range of D$ */
static inline int cache_writeback_dcache_range(uintptr_t addr, uint32_t length)
{
	int ret = cache_op_addr(addr, length, COP_OP_WRITEBACK_DCACHE_ADDR);
	cache_barrier_icache();
	cache_barrier_dcache();
	return ret;
}

/* invalidate all D$ */
static inline void cache_invalidate_dcache(void)
{
	cache_op_all(COP_OP_INVALIDATE_DCACHE);
}

/* invalidate a range of D$ */
static inline int cache_invalidate_dcache_range(uintptr_t addr, uint32_t length)
{
	return cache_op_addr(addr, length, COP_OP_INVALIDATE_DCACHE_ADDR);
}

/* writeback and invalidate all D$ */
static inline void cache_flush_dcache(void)
{
	cache_op_all(COP_OP_FLUSH_DCACHE);
	cache_barrier_icache();
	cache_barrier_dcache();
}

/* writeback and invalidate a range of D$ */
static inline int cache_flush_dcache_range(uintptr_t addr, uint32_t length)
{
	int ret = cache_op_addr(addr, length, COP_OP_FLUSH_DCACHE_ADDR);
	cache_barrier_icache();
	cache_barrier_dcache();
	return ret;
}

struct mpu_entry {
	/* 1k alignment and the address is inclusive */
	uintptr_t start_addr;
	/* 1k alignment in 4GB boundary and non-inclusive */
	uintptr_t end_addr;
	/* MPU_ATTR */
	uint32_t attribute;
};

void cache_init(void);

#ifdef DEBUG
int command_enable_pmu(int argc, const char **argv);
int command_disable_pmu(int argc, const char **argv);
int command_show_pmu(int argc, const char **argv);
#endif

#endif /* #ifndef __CROS_EC_CACHE_H */
