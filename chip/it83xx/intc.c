/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "registers.h"
#include "task.h"
#include "kmsc_chip.h"
#include "intc.h"

void intc_cpu_int_group_5(void)
{
	/* Determine interrupt number. */
	int intc_group_5 = IT83XX_INTC_IVCT5 - 16;

	switch (intc_group_5) {
#ifdef CONFIG_LPC
	case IT83XX_IRQ_KBC_OUT:
		lpc_kbc_obe_interrupt();
		break;

	case IT83XX_IRQ_KBC_IN:
		lpc_kbc_ibf_interrupt();
		break;
#endif
#if defined(HAS_TASK_KEYSCAN) && !defined(CONFIG_KEYBOARD_KSI_WUC_INT)
	case IT83XX_IRQ_KB_MATRIX:
		keyboard_raw_interrupt();
		break;
#endif
	default:
		break;
	}
}
DECLARE_IRQ(CPU_INT_GROUP_5, intc_cpu_int_group_5, 2);
