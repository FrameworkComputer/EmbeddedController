// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ec_commands.h"
#include "system.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

/*
 * The following functions are defined in upstream Zephyr:
 * Path: src/third_party/zephyrproject/zephyr/arch/riscv/core/pmp.c
 */
void pmp_decode_region(uint8_t cfg_byte, unsigned long *pmp_addr,
		       unsigned int index, unsigned long *start,
		       unsigned long *end);
void z_riscv_pmp_read_config(unsigned long *pmp_cfg, size_t pmp_cfg_size);
void z_riscv_pmp_read_addr(unsigned long *pmp_addr, size_t pmp_addr_size);

BUILD_ASSERT((CONFIG_PMP_SLOTS % sizeof(unsigned long)) == 0,
	     "CONFIG_PMP_SLOTS must be a multiple of register width");

/* Helper structure to define the expected PMP regions. */
struct pmp_region {
	uintptr_t base;
	size_t size;
	uint8_t perm;
	bool found;
};

/*
 * Hardcoded specifications for mandatory memory regions.
 * These are used to verify that the Physical Memory Protection (PMP)
 * configuration matches the expected base addresses, sizes, and
 * permissions for Null Pointer protection, ROM, and Rollback regions.
 */
static struct pmp_region expected_regions[] = {
	{ .base = 0,
	  .size = CONFIG_NULL_POINTER_EXCEPTION_REGION_SIZE,
	  .perm = 0x0,
	  .found = false },
	{ .base = (uintptr_t)__rom_region_start,
	  .size = (size_t)__rom_region_size,
	  .perm = PMP_R | PMP_X,
	  .found = false },
	{ .base = DT_REG_ADDR(DT_NODELABEL(rollback)),
	  .size = DT_REG_SIZE(DT_NODELABEL(rollback)),
	  .perm = 0x0,
	  .found = false },
};

LOG_MODULE_REGISTER(pmp_entries, LOG_LEVEL_INF);

ZTEST_SUITE(pmp_entries, NULL, NULL, NULL, NULL, NULL);

ZTEST(pmp_entries, test_pmp_entries)
{
	if (system_get_image_copy() == EC_IMAGE_RW) {
		LOG_INF("Running the test in RW.");
	} else {
		LOG_INF("Running the test in RO.");
	}

	const size_t num_pmpcfg_regs = CONFIG_PMP_SLOTS / sizeof(unsigned long);
	const size_t num_pmpaddr_regs = CONFIG_PMP_SLOTS;

	unsigned long current_pmpcfg_regs[num_pmpcfg_regs];
	unsigned long current_pmpaddr_regs[num_pmpaddr_regs];

	/* Read the current PMP configuration from the control registers */
	z_riscv_pmp_read_config(current_pmpcfg_regs, num_pmpcfg_regs);
	z_riscv_pmp_read_addr(current_pmpaddr_regs, num_pmpaddr_regs);

	const uint8_t *const current_pmp_cfg_entries =
		(const uint8_t *)current_pmpcfg_regs;

	for (unsigned int index = 0; index < CONFIG_PMP_SLOTS; ++index) {
		unsigned long start, end;
		uint8_t cfg_byte = current_pmp_cfg_entries[index];

		/*
		 * Decode the configured PMP region into absolute addresses.
		 * Both 'start' and 'end' are the inclusive bounds of the pmp
		 * entry: [start, end]. The 'end' is (start + size - 1).
		 */
		pmp_decode_region(cfg_byte, current_pmpaddr_regs, index, &start,
				  &end);

		/*
		 * Compare the decoded region against the list of expected
		 * regions
		 */
		for (size_t i = 0; i < ARRAY_SIZE(expected_regions); ++i) {
			if ((start == expected_regions[i].base) &&
			    (end == expected_regions[i].base +
					    expected_regions[i].size - 1) &&
			    ((cfg_byte & (PMP_R | PMP_W | PMP_X | PMP_L)) ==
			     expected_regions[i].perm)) {
				expected_regions[i].found = true;
				break;
			}
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(expected_regions); i++) {
		zassert_true(
			expected_regions[i].found,
			"PMP entry for region %zu (base 0x%lx, size 0x%zx, perm 0x%x) not "
			"found.",
			i + 1, expected_regions[i].base,
			expected_regions[i].size, expected_regions[i].perm);
	}
}
