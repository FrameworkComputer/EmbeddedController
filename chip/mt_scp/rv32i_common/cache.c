/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cache.h"
#include "console.h"
#include "csr.h"

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

#ifdef CHIP_FAMILY_RV55
	set_csr(CSR_MCTREN, CSR_MCTREN_BTB);
	set_csr(CSR_MCTREN, CSR_MCTREN_TLP);
#endif

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
			write_csr(CSR_MPU_L(i),
				  mpu_entries[i].start_addr |
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
	asm volatile("fence.i" ::: "memory");
}

#ifdef DEBUG
/*
 * I for I-cache
 * D for D-cache
 * C for control transfer instructions (branch, jump, ret, interrupt, ...)
 */
static enum { PMU_SELECT_I = 0, PMU_SELECT_D, PMU_SELECT_C } pmu_select;

static int command_enable_pmu(int argc, const char **argv)
{
	static const char *const selectors[] = {
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
		  CSR_PMU_MPMUCTR_C | CSR_PMU_MPMUCTR_I | CSR_PMU_MPMUCTR_H3 |
			  CSR_PMU_MPMUCTR_H4 | CSR_PMU_MPMUCTR_H5);

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
		CSR_PMU_MPMUCTR_C | CSR_PMU_MPMUCTR_I | CSR_PMU_MPMUCTR_H3 |
			CSR_PMU_MPMUCTR_H4 | CSR_PMU_MPMUCTR_H5);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(enable_pmu, command_enable_pmu, "[I | D | C]",
			     "Enable PMU");

static int command_disable_pmu(int argc, const char **argv)
{
	clear_csr(CSR_PMU_MPMUCTR,
		  CSR_PMU_MPMUCTR_C | CSR_PMU_MPMUCTR_I | CSR_PMU_MPMUCTR_H3 |
			  CSR_PMU_MPMUCTR_H4 | CSR_PMU_MPMUCTR_H5);
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(disable_pmu, command_disable_pmu, NULL,
			     "Disable PMU");

static int command_show_pmu(int argc, const char **argv)
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
		ccprintf("  miss-predict: %lld (%d.%d%%)\n", val4, p / 100,
			 p % 100);
		ccprintf("interrupts: %lld\n", val5);
		break;
	}

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(show_pmu, command_show_pmu, NULL, "Show PMU");
#endif
