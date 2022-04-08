/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* MT SCP RV32i configuration */

#include "cache.h"
#include "csr.h"
#include "hooks.h"
#include "panic.h"
#include "registers.h"

#define SCP_SRAM_END (CONFIG_IPC_SHARED_OBJ_ADDR & (~(0x400 - 1)))

struct mpu_entry mpu_entries[NR_MPU_ENTRIES] = {
	/* SRAM (for most code, data) */
	{ 0, SCP_SRAM_END, MPU_ATTR_C | MPU_ATTR_W | MPU_ATTR_R },
	/* SRAM (for IPI shared buffer) */
	{ SCP_SRAM_END, SCP_FW_END, MPU_ATTR_W | MPU_ATTR_R },
/* For AP domain */
#if defined(CHIP_VARIANT_MT8195) || defined(CHIP_VARIANT_MT8188)
	{ AP_REG_BASE, AP_REG_BASE + 0x10000000,
	  MPU_ATTR_W | MPU_ATTR_R | MPU_ATTR_P },
#else
	{ AP_REG_BASE, AP_REG_BASE + 0x10000000, MPU_ATTR_W | MPU_ATTR_R },
#endif

#if !defined(CHIP_VARIANT_MT8188)
	/* For SCP sys */
	{ 0x70000000, 0x80000000, MPU_ATTR_W | MPU_ATTR_R },
#endif

#if defined(CHIP_VARIANT_MT8195) || defined(CHIP_VARIANT_MT8188)
	{ CONFIG_DRAM_BASE, DRAM_NC_BASE,
	  MPU_ATTR_C | MPU_ATTR_W | MPU_ATTR_R },
	{ DRAM_NC_BASE, KERNEL_BASE + KERNEL_SIZE, MPU_ATTR_W | MPU_ATTR_R },
#else
	{ 0x10000000, 0x11400000, MPU_ATTR_W | MPU_ATTR_R },
#endif
};

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

#ifdef CONFIG_PANIC_CONSOLE_OUTPUT
static void report_previous_panic(void)
{
	struct panic_data *panic = panic_get_data();

	if (panic == NULL && SCP_CORE_MON_PC_LATCH == 0)
		return;

	ccprintf("[Previous Panic]\n");
	if (panic) {
		panic_data_ccprint(panic);
	} else {
		ccprintf("No panic data\n");
	}
	ccprintf("Latch PC:%x LR:%x SP:%x\n", SCP_CORE_MON_PC_LATCH,
		 SCP_CORE_MON_LR_LATCH, SCP_CORE_MON_SP_LATCH);
}
DECLARE_HOOK(HOOK_INIT, report_previous_panic, HOOK_PRIO_DEFAULT);
#endif
