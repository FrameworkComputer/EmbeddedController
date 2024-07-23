/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "config.h"
#include "rwsig.h"

OUTPUT_FORMAT(BFD_FORMAT, BFD_FORMAT, BFD_FORMAT)
OUTPUT_ARCH(BFD_ARCH)
ENTRY(reset)

SECTIONS
{
	. = CONFIG_ISH_BOOT_START; /* ISH SRAM (640KB) begins at 0xFF000000 */

	__aon_ro_start = .;

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
		 KEEP(*(SORT(.rodata.hcmds*)))
		 __hcmds_end = .;

		 . = ALIGN(4);
		 __mkbp_evt_srcs = .;
		 KEEP(*(.rodata.evtsrcs))
		 __mkbp_evt_srcs_end = .;

		 . = ALIGN(4);
		__hooks_init = .;
		KEEP(*(.rodata.HOOK_INIT))
		__hooks_init_end = .;

		__hooks_init_early = .;
		KEEP(*(.rodata.HOOK_INIT_EARLY))
		__hooks_init_early_end = .;

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

#ifdef CONFIG_CHIPSET_RESUME_INIT_HOOK
		__hooks_chipset_resume_init = .;
		KEEP(*(.rodata.HOOK_CHIPSET_RESUME_INIT))
		__hooks_chipset_resume_init_end = .;

		__hooks_chipset_suspend_complete = .;
		KEEP(*(.rodata.HOOK_CHIPSET_SUSPEND_COMPLETE))
		__hooks_chipset_suspend_complete_end = .;
#endif

		__hooks_chipset_shutdown = .;
		KEEP(*(.rodata.HOOK_CHIPSET_SHUTDOWN))
		__hooks_chipset_shutdown_end = .;

		__hooks_chipset_shutdown_complete = .;
		KEEP(*(.rodata.HOOK_CHIPSET_SHUTDOWN_COMPLETE))
		__hooks_chipset_shutdown_complete_end = .;

		__hooks_chipset_hard_off = .;
		KEEP(*(.rodata.HOOK_CHIPSET_HARD_OFF))
		__hooks_chipset_hard_off_end = .;

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

#ifdef CONFIG_BODY_DETECTION
		__hooks_body_detect_change = .;
		KEEP(*(.rodata.HOOK_BODY_DETECT_CHANGE))
		__hooks_body_detect_change_end = .;
#endif

		__hooks_base_attached_change = .;
		KEEP(*(.rodata.HOOK_BASE_ATTACHED_CHANGE))
		__hooks_base_attached_change_end = .;

		__hooks_pwrbtn_change = .;
		KEEP(*(.rodata.HOOK_POWER_BUTTON_CHANGE))
		__hooks_pwrbtn_change_end = .;

		__hooks_battery_soc_change = .;
		KEEP(*(.rodata.HOOK_BATTERY_SOC_CHANGE))
		__hooks_battery_soc_change_end = .;

#ifdef CONFIG_USB_SUSPEND
		__hooks_usb_change = .;
		KEEP(*(.rodata.HOOK_USB_PM_CHANGE))
		__hooks_usb_change_end = .;
#endif

		__hooks_tick = .;
		KEEP(*(.rodata.HOOK_TICK))
		__hooks_tick_end = .;

		__hooks_second = .;
		KEEP(*(.rodata.HOOK_SECOND))
		__hooks_second_end = .;

		__hooks_usb_pd_disconnect = .;
		KEEP(*(.rodata.HOOK_USB_PD_DISCONNECT))
		__hooks_usb_pd_disconnect_end = .;

		__hooks_usb_pd_connect = .;
		KEEP(*(.rodata.HOOK_USB_PD_CONNECT))
		__hooks_usb_pd_connect_end = .;

		__hooks_power_supply_change = .;
		KEEP(*(.rodata.HOOK_POWER_SUPPLY_CHANGE))
		__hooks_power_supply_change_end = .;

		__deferred_funcs = .;
		KEEP(*(.rodata.deferred))
		__deferred_funcs_end = .;

		 . = ALIGN(4);
		 KEEP(*(.rodata.*))

#ifdef CONFIG_CHIP_INIT_ROM_REGION
	ASSERT(0, "CONFIG_CHIP_INIT_ROM_REGION not supported by linker script")
#endif /* CONFIG_CHIP_INIT_ROM_REGION */
		/*
		 * This linker file does not yet support a separate ROM resident
		 * section. Ensure the corresponding data objects are linked
		 * into the .rodata section.
		 */
		. = ALIGN(4);
		__init_rom_start = .;
		*(.init.rom)
		__init_rom_end = .;
	}

	/*
	 * ISH DMA need 64 bytes address align, in D0i3 low power state
	 * need copy RW part to IMR DDR via DMA
	 */
	. = ALIGN(64);
	__aon_ro_end = .;

	__aon_rw_start = .;

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

		/*
		 * Reserve space for deferred function firing times.
		 * Each time is a uint64_t, each func is a 32-bit
		 * pointer, thus the scaling factor of two.  The 8
		 * byte alignment of uint64_t is required by the ABI.
		 */

		 . = ALIGN(8);
		 __deferred_until = .;
		 . += (__deferred_funcs_end - __deferred_funcs) * (8 / 4);
		 __deferred_until_end = .;

		 __bss_end = .;
		 __bss_size_words = ABSOLUTE((__bss_end - __bss_start) / 4);

		/*
		 * Shared memory buffer must be at the end of
		 * preallocated RAM, so it can expand to use all the
		 * remaining RAM.
		 */
		__shared_mem_buf = .;
	}

	__aon_rw_end = . + CONFIG_SHAREDMEM_MINIMUM_SIZE;

	ASSERT((__shared_mem_buf + CONFIG_SHAREDMEM_MINIMUM_SIZE) <=
	       (CONFIG_RAM_BASE + CONFIG_RAM_SIZE),
	       "Not enough space for shared memory.")
	__ram_free = (CONFIG_RAM_BASE + CONFIG_RAM_SIZE) -
		     (__shared_mem_buf + CONFIG_SHAREDMEM_MINIMUM_SIZE);

	/* TODO: Adjust __image_size to proper value for core minute-ia later */
	__image_size = . - CONFIG_ISH_BOOT_START;

#if defined(SECTION_IS_RW) && (CONFIG_FLASH_WRITE_SIZE > 0)
	/* Alignment is needed by flash_command_write() for RW update. */
	ASSERT(__image_size % CONFIG_FLASH_WRITE_SIZE == 0,
	       "Error: RW image is not aligned to CONFIG_FLASH_WRITE_SIZE")
#endif

	def_irq_low  = ABSOLUTE(default_int_handler) & 0xFFFF;
	def_irq_high = ABSOLUTE(default_int_handler) >> 16;

#ifdef CONFIG_ISH_PM_AONTASK
	ish_persistent_data_aon = ABSOLUTE(CONFIG_AON_PERSISTENT_BASE);
#endif
}
