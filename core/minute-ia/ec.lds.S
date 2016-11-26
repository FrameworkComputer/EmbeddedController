/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "config.h"
#include "rsa.h"

OUTPUT_FORMAT(BFD_FORMAT, BFD_FORMAT, BFD_FORMAT)
OUTPUT_ARCH(BFD_ARCH)
ENTRY(reset)

SECTIONS
{
	. = CONFIG_ISH_BOOT_START; /* ISH SRAM (640KB) begins at 0xFF000000 */

	/* .init section should be first, since it contains the boot code */
	.init : { *(.init*); }
	.text : { *(.text.*); }
	.text.unlikely : { *(.text.unlikely*); }

	. = ALIGN(4);
	.rodata : {
		/* Symbols defined here are declared in link_defs.h */
		 __irqprio = .;
	        KEEP(*(.rodata.irqprio))
		 __irqprio_end = .;

		 __irq_data = .;
	        KEEP(*(.rodata.irqs))
		 __irq_data_end = .;

		. = ALIGN(4);
		 __cmds = .;
		 KEEP(*(SORT(.rodata.cmds*)))
		 __cmds_end = .;

		 . = ALIGN(4);
		 __extension_cmds = .;
		 KEEP(*(.rodata.extensioncmds))
		 __extension_cmds_end = .;

		 . = ALIGN(4);
		 __hcmds = .;
		 KEEP(*(.rodata.hcmds))
		 __hcmds_end = .;

		 . = ALIGN(4);
		 __mkbp_evt_srcs = .;
		 KEEP(*(.rodata.evtsrcs))
		 __mkbp_evt_srcs_end = .;

		 . = ALIGN(4);
		__hooks_init = .;
		KEEP(*(.rodata.HOOK_INIT))
		__hooks_init_end = .;

		__hooks_pre_freq_change = .;
		KEEP(*(.rodata.HOOK_PRE_FREQ_CHANGE))
		__hooks_pre_freq_change_end = .;

		__hooks_freq_change = .;
		KEEP(*(.rodata.HOOK_FREQ_CHANGE))
		__hooks_freq_change_end = .;

		__hooks_sysjump = .;
		KEEP(*(.rodata.HOOK_SYSJUMP))
		__hooks_sysjump_end = .;

		__hooks_chipset_pre_init = .;
		KEEP(*(.rodata.HOOK_CHIPSET_PRE_INIT))
		__hooks_chipset_pre_init_end = .;

		__hooks_chipset_startup = .;
		KEEP(*(.rodata.HOOK_CHIPSET_STARTUP))
		__hooks_chipset_startup_end = .;

		__hooks_chipset_resume = .;
		KEEP(*(.rodata.HOOK_CHIPSET_RESUME))
		__hooks_chipset_resume_end = .;

		__hooks_chipset_suspend = .;
		KEEP(*(.rodata.HOOK_CHIPSET_SUSPEND))
		__hooks_chipset_suspend_end = .;

		__hooks_chipset_shutdown = .;
		KEEP(*(.rodata.HOOK_CHIPSET_SHUTDOWN))
		__hooks_chipset_shutdown_end = .;

		__hooks_chipset_reset = .;
		KEEP(*(.rodata.HOOK_CHIPSET_RESET))
		__hooks_chipset_reset_end = .;

		__hooks_ac_change = .;
		KEEP(*(.rodata.HOOK_AC_CHANGE))
		__hooks_ac_change_end = .;

		__hooks_lid_change = .;
		KEEP(*(.rodata.HOOK_LID_CHANGE))
		__hooks_lid_change_end = .;

		__hooks_tablet_mode_change = .;
		KEEP(*(.rodata.HOOK_TABLET_MODE_CHANGE))
		__hooks_tablet_mode_change_end = .;

		__hooks_pwrbtn_change = .;
		KEEP(*(.rodata.HOOK_POWER_BUTTON_CHANGE))
		__hooks_pwrbtn_change_end = .;

		__hooks_charge_state_change = .;
		KEEP(*(.rodata.HOOK_CHARGE_STATE_CHANGE))
		__hooks_charge_state_change_end = .;

		__hooks_battery_soc_change = .;
		KEEP(*(.rodata.HOOK_BATTERY_SOC_CHANGE))
		__hooks_battery_soc_change_end = .;

		__hooks_tick = .;
		KEEP(*(.rodata.HOOK_TICK))
		__hooks_tick_end = .;

		__hooks_second = .;
		KEEP(*(.rodata.HOOK_SECOND))
		__hooks_second_end = .;

		__deferred_funcs = .;
		KEEP(*(.rodata.deferred))
		__deferred_funcs_end = .;

		 . = ALIGN(4);
		 KEEP(*(.rodata.*))
	}

	.data : {
		 __data_start = .;
		*(.data.*);
		 __data_end = .;
	 }

	. = ALIGN(4);
	.note.gnu.build-id : { *(.note.gnu.build-id); }

	.bss : {
		__bss_start = .;

		*(.bss*);
		*(COMMON*);

		# Reserve space for deferred function firing times.  Each time
		# is a uint64_t, each func is a 32-bit pointer, thus the scaling
		# factor of two.  The 8 byte alignment of uint64_t is required
		# by the ABI.

		 . = ALIGN(8);
		 __deferred_until = .;
		 . += (__deferred_funcs_end - __deferred_funcs) * (8 / 4);
		 __deferred_until_end = .;

		 __bss_end = .;
		__bss_size_words = (__bss_end - __bss_start) / 4;
	}

	def_irq_low  = ABSOLUTE(default_int_handler) & 0xFFFF;
	def_irq_high = ABSOLUTE(default_int_handler) >> 16;
}
