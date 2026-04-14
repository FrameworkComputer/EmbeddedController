/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "system.h"

#include <zephyr/devicetree.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util_macro.h>

#include <chip_chipregs.h>
#include <cpu.h>
#include <ilm.h>

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ilm), okay)
/*
 * Bit 3 of the SCAR High byte (SCARnH) disables the ILM mapping when set to 1.
 * It puts the RAM block into DLM (Data Local Memory) mode, bypassing ILM flash
 * interception.
 */
#define IT8XXX2_SMFI_SCARH_DISABLE BIT(3)

/*
 * The SCAR registers are 3 bytes wide (Low, Middle, High).
 * DT_REG_ADDR_BY_IDX returns the base address (the Low byte).
 */
#define DISABLE_ILM_MAPPING(idx, node_id)                          \
	do {                                                       \
		uintptr_t addr = DT_REG_ADDR_BY_IDX(node_id, idx); \
		sys_write8(IT8XXX2_SMFI_SCARH_DISABLE, addr + 2);  \
	} while (0)

static void ilm_sysjump_disable(void)
{
	unsigned int key;

	/* Disable all ILM mappings before sysjump, otherwise the new image
	 * may attempt to execute stale code from the previous image.
	 */
	key = irq_lock();
	LISTIFY(DT_NUM_REGS(DT_NODELABEL(ilm)), DISABLE_ILM_MAPPING, (;),
		DT_NODELABEL(ilm));
	irq_unlock(key);
}
DECLARE_HOOK(HOOK_SYSJUMP, ilm_sysjump_disable, HOOK_PRIO_LAST);
#endif

uintptr_t system_get_fw_reset_vector(uintptr_t base)
{
	/*
	 * Because our reset vector is at the beginning of image copy
	 * (see init.S). So I just need to return 'base' here and EC will jump
	 * to the reset vector.
	 */
	return base;
}
