/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <arch/cpu.h>
#include <fatal.h>
#include <logging/log.h>
#include <logging/log_ctrl.h>
#include <zephyr.h>

#include "panic.h"

/*
 * Arch-specific configuration
 *
 * For each architecture, define:
 * - PANIC_ARCH, which should be the corresponding arch field of the
 *   panic_data struct.
 * - PANIC_REG_LIST, which is a macro that takes a parameter M, and
 *   applies M to 3-tuples of:
 *   - zephyr esf field name
 *   - panic_data struct field name
 *   - human readable name
 */

#ifdef CONFIG_ARM
#define PANIC_ARCH PANIC_ARCH_CORTEX_M
#define PANIC_REG_LIST(M)             \
	M(basic.r0, cm.frame[0], a1)  \
	M(basic.r1, cm.frame[1], a2)  \
	M(basic.r2, cm.frame[2], a3)  \
	M(basic.r3, cm.frame[3], a4)  \
	M(basic.r12, cm.frame[4], ip) \
	M(basic.lr, cm.frame[5], lr)  \
	M(basic.pc, cm.frame[6], pc)  \
	M(basic.xpsr, cm.frame[7], xpsr)
#else
/* Not implemented for this arch */
#define PANIC_ARCH 0
#define PANIC_REG_LIST(M)
#endif

/* Macros to be applied to PANIC_REG_LIST as M */
#define PANIC_COPY_REGS(esf_field, pdata_field, human_name) \
	pdata->pdata_field = esf->esf_field;
#define PANIC_PRINT_REGS(esf_field, pdata_field, human_name) \
	panic_printf("  %-8s = 0x%08X\n", #human_name, pdata->pdata_field);

static void copy_esf_to_panic_data(const z_arch_esf_t *esf,
				   struct panic_data *pdata)
{
	pdata->arch = PANIC_ARCH;
	pdata->struct_version = 2;
	pdata->flags = 0;
	pdata->reserved = 0;
	pdata->struct_size = sizeof(*pdata);
	pdata->magic = PANIC_DATA_MAGIC;

	PANIC_REG_LIST(PANIC_COPY_REGS);
}

void panic_data_print(const struct panic_data *pdata)
{
	PANIC_REG_LIST(PANIC_PRINT_REGS);
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
	panic_printf("Fatal error: %u\n", reason);

	if (PANIC_ARCH && esf) {
		copy_esf_to_panic_data(esf, get_panic_data_write());
		panic_data_print(panic_get_data());
	}

	LOG_PANIC();
	k_fatal_halt(reason);
	CODE_UNREACHABLE;
}
