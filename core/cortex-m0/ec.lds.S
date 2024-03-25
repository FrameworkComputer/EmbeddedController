/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "rwsig.h"

#define STRINGIFY0(name) #name
#define STRINGIFY(name) STRINGIFY0(name)

#define FW_OFF_(section) CONFIG_##section##_MEM_OFF
#define FW_OFF(section) (CONFIG_PROGRAM_MEMORY_BASE + FW_OFF_(section))

#define FW_SIZE_(section) CONFIG_##section##_SIZE
#define FW_SIZE(section) FW_SIZE_(section)

OUTPUT_FORMAT(BFD_FORMAT, BFD_FORMAT, BFD_FORMAT)
OUTPUT_ARCH(BFD_ARCH)
ENTRY(reset)

MEMORY
{
#ifdef CONFIG_SHAREDLIB
	SHARED_LIB (rx) : ORIGIN = FW_OFF(SHAREDLIB),
			  LENGTH = FW_SIZE(SHAREDLIB)
#endif
	FLASH (rx) : ORIGIN = FW_OFF(SECTION), LENGTH = FW_SIZE(SECTION)
	IRAM (rw)  : ORIGIN = CONFIG_RAM_BASE, LENGTH = CONFIG_RAM_SIZE

#ifdef CONFIG_CHIP_MEMORY_REGIONS
#define REGION(name, attr, start, size) \
	name(attr) : ORIGIN = (start), LENGTH = (size)
#include "memory_regions.inc"
#undef REGION
#endif /* CONFIG_MEMORY_REGIONS */
}

SECTIONS
{
#ifdef CONFIG_SHAREDLIB
	.roshared : {
		KEEP(*(.roshared*))
	} > SHARED_LIB
#endif
	.text : {
		*(.text.vecttable)
		. = ALIGN(4);
		__image_data_offset = .;
		KEEP(*(.rodata.ver))

		. = ALIGN(4);
		KEEP(*(.rodata.pstate))

		. = ALIGN(4);
		STRINGIFY(OUTDIR/core/CORE/init.o) (.text)
		*(.text*)
	} > FLASH

	. = ALIGN(4);
	.rodata : {
		/* Symbols defined here are declared in link_defs.h */
		__irqprio = .;
		KEEP(*(.rodata.irqprio))
		__irqprio_end = .;

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
		KEEP(*(.rodata.HOOK_EARLY_INIT))
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

		__usb_desc = .;
		KEEP(*(.rodata.usb_desc_conf))
		KEEP(*(SORT(.rodata.usb_desc*)))
		__usb_desc_end = .;
		. = ALIGN(4);
		KEEP(*(.rodata.usb_ep))
		KEEP(*(.rodata.usb_ep.usb_ep_tx))
		KEEP(*(.rodata.usb_ep.usb_ep_rx))
		KEEP(*(.rodata.usb_ep.usb_ep_event))
		KEEP(*(.rodata.usb_ep.usb_iface_request))

		. = ALIGN(4);
		*(.rodata*)

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

#if defined(SECTION_IS_RO) && defined(CONFIG_FLASH_CROS)
		. = ALIGN(64);
		KEEP(*(.google))
#endif
		. = ALIGN(4);
	} > FLASH

	__data_lma_start = . ;
	.vtable : {
		/*
		 * Vector table must be at the base of SRAM. The vector
		 * table section contains a RAM copy of the vector table used on
		 * STM chips for relocating the vector table.
		 */
		. = ALIGN(8);
		*(.bss.vector_table)
		. = ALIGN(8);
	} > IRAM

#ifdef CONFIG_PRESERVE_LOGS
	.preserve_logs(NOLOAD) : {
		/*
		 * The size of the vector table is fixed. Thus, the address of
		 * the preserved logs is also fixed.
		 */
		. = ALIGN(8);
		*(SORT(.preserved_logs.*))
		. = ALIGN(8);
		__preserved_logs_end = .;
	} > IRAM

	ASSERT((SIZEOF(.vtable) + SIZEOF(.preserve_logs) + CONFIG_RAM_BASE) ==
	       __preserved_logs_end,
	       "preserve_logs must be right after vtable")
#endif

	.bss : {
		. = ALIGN(8);
		__bss_start = .;
		/* Stacks must be 64-bit aligned */
		. = ALIGN(8);
		*(.bss.system_stack)
		/* Rest of .bss takes care of its own alignment */
		*(.bss*)

		/*
		 * Reserve space for deferred function firing times.
		 * Each time is a uint64_t, each func is a 32-bit pointer,
		 * thus the scaling factor of two. The 8 byte alignment of
		 * uint64_t is required by the ARM ABI.
		 */
		. = ALIGN(8);
		__deferred_until = .;
		. += (__deferred_funcs_end - __deferred_funcs) * (8 / 4);
		__deferred_until_end = .;

		. = ALIGN(4);
		__bss_end = .;
	} > IRAM

	.data : AT(ADDR(.rodata) + SIZEOF(.rodata)) {
		. = ALIGN(4);
		__data_start = .;
		*(.data.tasks)
		*(.data*)
		. = ALIGN(4);
		*(.iram.text)
		. = ALIGN(4);
		__data_end = .;

		/*
		 * Shared memory buffer must be at the end of preallocated
		 * RAM, so it can expand to use all the remaining RAM.
		 */
		__shared_mem_buf = .;

		/* NOTHING MAY GO AFTER THIS! */
	} > IRAM

	ASSERT((__shared_mem_buf + CONFIG_SHAREDMEM_MINIMUM_SIZE) <=
	       (CONFIG_RAM_BASE + CONFIG_RAM_SIZE),
	       "Not enough space for shared memory.")

	__ram_free = (CONFIG_RAM_BASE + CONFIG_RAM_SIZE) -
		     (__shared_mem_buf + CONFIG_SHAREDMEM_MINIMUM_SIZE);

	/*
	 * Relocations are not supported, so make sure that there is no GOT
	 * section. See http://issuetracker.google.com/234507656#comment15.
	 */
	.got (NOLOAD) :
	{
		KEEP(*(.got*));
	}
	ASSERT(SIZEOF(.got) == 0, "GOT section found, but not supported.\
 Make sure that all code is compiled without -fPIC.")

	/*
	 * __flash_used and __flash_size is used in flash free calculations by
	 * the makefile. Note the difference between __flash_size and
	 * __config_flash_size. __flash_size is the flash size for the given
	 * image (RW, RO, etc.), while __config_flash_size is the *total*
	 * flash size.
	 * __image_size is stored in the struct image_data header and used
	 * in hash calculations.
	 */
	__flash_used = LOADADDR(.data) + SIZEOF(.data) - ORIGIN(FLASH);
	__flash_size = LENGTH(FLASH);
	__image_size = __flash_used;

#if defined(SECTION_IS_RW) && (CONFIG_FLASH_WRITE_SIZE > 0)
	/* Alignment is needed by flash_command_write() for RW update. */
	ASSERT(__image_size % CONFIG_FLASH_WRITE_SIZE == 0,
	       "Error: RW image is not aligned to CONFIG_FLASH_WRITE_SIZE")
#endif

#ifdef CONFIG_FLASH_CROS
        /*
         * These linker labels are just for analysis and not used in the code.
         */
	__config_flash_size = CONFIG_FLASH_SIZE_BYTES;
	__config_ro_size = CONFIG_RO_SIZE;
	__config_ec_protected_storage_size = CONFIG_EC_PROTECTED_STORAGE_SIZE;
	__config_rw_size = CONFIG_RW_SIZE;
	__config_ec_writable_storage_size = CONFIG_EC_WRITABLE_STORAGE_SIZE;
#endif

	/*
	 * The linker won't notice if the .data section is too big to fit,
	 * apparently because we're sending it into IRAM, not FLASH.
	 * Verify that all sections linked into the FLASH region will fit.
	 */
	ASSERT((LENGTH(FLASH)
#if defined(CONFIG_RWSIG) && defined(SECTION_IS_RO)
	       - CONFIG_RO_PUBKEY_SIZE
#endif
#if defined(CONFIG_RWSIG) && defined(SECTION_IS_RW)
	       - CONFIG_RW_SIG_SIZE
#endif
	       ) >= __flash_used,
	       "No room left in the flash")

	__image_size = __flash_used;

#ifdef CONFIG_CHIP_MEMORY_REGIONS
#define REGION(name, attr, start, size) \
	.name(NOLOAD) : { \
		__##name##_start = .; \
		*(SORT(.name.*)) \
	} > name
#include "memory_regions.inc"
#undef REGION
#endif /* CONFIG_CHIP_MEMORY_REGIONS */

#if !(defined(SECTION_IS_RO) && defined(CONFIG_FLASH_CROS))
    /DISCARD/ : { *(.google) }
#endif
    /DISCARD/ : { *(.ARM.*) }
}
