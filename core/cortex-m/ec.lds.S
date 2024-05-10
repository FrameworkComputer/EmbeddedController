/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "rwsig.h"

#define CONCAT_STAGE_1(w, x, y, z) w ## x ## y ## z
#define CONCAT2(w, x) CONCAT_STAGE_1(w, x, , )
#define CONCAT3(w, x, y) CONCAT_STAGE_1(w, x, y, )
#define CONCAT4(w, x, y, z) CONCAT_STAGE_1(w, x, y, z)

#define STRINGIFY0(name) #name
#define STRINGIFY(name) STRINGIFY0(name)

#ifdef RW_B_LDS
#define FW_MEM_OFF_(section) CONFIG_##section##_B_MEM_OFF
#else
#define FW_MEM_OFF_(section) CONFIG_##section##_MEM_OFF
#endif
#define FW_MEM_OFF(section) (FW_MEM_OFF_(section))
#define FW_OFF(section) (CONFIG_PROGRAM_MEMORY_BASE + FW_MEM_OFF(section))

#define FW_SIZE_(section) CONFIG_##section##_SIZE
#define FW_SIZE(section) FW_SIZE_(section)

/*
 * Define the VMA (virtual memory address) of the ROM_RESIDENT region within
 * the EC image. This is full 32-bit address starting from
 * CONFIG_PROGRAM_MEMORY_BASE.
 */
#define ROM_RES_OFF(section) FW_OFF(CONCAT2(section, _ROM_RESIDENT))
#define ROM_RES_SIZE(section) FW_SIZE(CONCAT2(section, _ROM_RESIDENT))

/*
 * Define the VMA (virtual memory address) of the ROM_RESIDENT region.  Instead
 * of a full 32-bit address, set the VMA to be an offset within the flash memory
 * section.  Objects linked into this section can pass the address of the
 * object unmodified to the public APIs of the flash and init_rom modules.
 */
#ifdef SECTION_IS_RO
#define ROM_RES_FLASH_OFF(section) \
	FW_MEM_OFF(CONCAT2(section, _ROM_RESIDENT)) + \
	CONFIG_EC_PROTECTED_STORAGE_OFF
#else
#define ROM_RES_FLASH_OFF(section) \
	FW_MEM_OFF(CONCAT2(section, _ROM_RESIDENT)) + \
	CONFIG_EC_WRITABLE_STORAGE_OFF
#endif

/* Indicates where .data LMA should reside. */
#undef DATA_LMA_MEM_REGION

OUTPUT_FORMAT(BFD_FORMAT, BFD_FORMAT, BFD_FORMAT)
OUTPUT_ARCH(BFD_ARCH)
ENTRY(reset)

MEMORY
{
#if !defined(CONFIG_FLASH_PHYSICAL)
	IROM (rx) : ORIGIN = CONFIG_ROM_BASE, LENGTH = CONFIG_ROM_SIZE
#else
#if defined(SECTION_IS_RO) && defined(NPCX_RO_HEADER)
	/*
	 * Header structure used by npcx booter in RO region.
	 * Please notice the location of header must be in front of FW
	 * which needs copy. But header itself won't be copied to code ram
	 * by booter.
	 */
	FLASH_HDR (rx) : ORIGIN = FW_OFF(RO_HDR), LENGTH = FW_SIZE(RO_HDR)
	FLASH (rx) : ORIGIN = FW_OFF(SECTION) + FW_SIZE(RO_HDR), \
		     LENGTH = FW_SIZE(SECTION) - FW_SIZE(RO_HDR)
#else
	FLASH (rx) : ORIGIN = FW_OFF(SECTION), LENGTH = FW_SIZE(SECTION)
#endif
#ifdef CONFIG_CHIP_INIT_ROM_REGION
	ROM_RESIDENT (r) : \
		ORIGIN = ROM_RES_OFF(SECTION), \
		LENGTH = ROM_RES_SIZE(SECTION)

	ROM_RESIDENT_VMA (r) : \
		ORIGIN = ROM_RES_FLASH_OFF(SECTION), \
		LENGTH = ROM_RES_SIZE(SECTION)
#endif /* CONFIG_CHIP_INIT_ROM_REGION */
#ifdef CONFIG_SHAREDLIB
	SHARED_LIB (rx) : ORIGIN = FW_OFF(SHAREDLIB), \
			  LENGTH = FW_SIZE(SHAREDLIB)
#endif
#endif /* !CONFIG_FLASH_PHYSICAL */
	IRAM (rw) : ORIGIN = CONFIG_RAM_BASE, LENGTH = CONFIG_RAM_SIZE

#ifdef CONFIG_EXTERNAL_STORAGE
	CDRAM (rx) : \
		ORIGIN = CONFIG_PROGRAM_MEMORY_BASE + FW_MEM_OFF(SECTION), \
		LENGTH = FW_SIZE(SECTION)
#endif /* CONFIG_EXTERNAL_STORAGE */

#ifdef CONFIG_CHIP_MEMORY_REGIONS
#define REGION(name, attr, start, size) \
	name(attr) : ORIGIN = (start), LENGTH = (size)
#define REGION_LOAD REGION
#include "memory_regions.inc"
#undef REGION
#undef REGION_LOAD
#endif /* CONFIG_MEMORY_REGIONS */

#ifdef CONFIG_DRAM_BASE
	DRAM (rwx) : ORIGIN = CONFIG_DRAM_BASE, LENGTH = CONFIG_DRAM_SIZE
#endif
}

/*
 * Convenience macros for determining the correct output memory section.
 */
#if !defined(CONFIG_FLASH_PHYSICAL)
	#define EC_IMAGE_LMA_MEM_REGION		IROM
	#define EC_IMAGE_VMA_MEM_REGION		IROM
	#define DATA_LMA_MEM_REGION		IROM
#else
	#define EC_IMAGE_LMA_MEM_REGION		FLASH
	#ifdef CONFIG_EXTERNAL_STORAGE
		#define EC_IMAGE_VMA_MEM_REGION	CDRAM
	#else
		#define EC_IMAGE_VMA_MEM_REGION	FLASH
	#endif

	#ifdef CONFIG_CHIP_DATA_IN_INIT_ROM
		#define DATA_LMA_MEM_REGION	ROM_RESIDENT
	#else
		#define DATA_LMA_MEM_REGION	FLASH
	#endif
#endif

SECTIONS
{
#if defined(SECTION_IS_RO) && defined(NPCX_RO_HEADER)
	.header : {
		KEEP(*(.header))
	} > FLASH_HDR
#endif
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
#if NPCX_FAMILY_VERSION >= NPCX_FAMILY_NPCX7 && !defined(CONFIG_HIBERNATE_PSL)
		/* Keep hibernate utility in last code ram block */
		. = ALIGN(4);
		KEEP(*(.after_init))
		__after_init_end = .;
#endif
		*(.text*)
#ifdef CONFIG_EXTERNAL_STORAGE
		. = ALIGN(4);
		__flash_lpfw_start = .;
		/* Entering deep idle FW for better power consumption */
		KEEP(*(.lowpower_ram))
		. = ALIGN(4);
		__flash_lpfw_end = .;
		__flash_lplfw_start = .;
		/* GDMA utilities for better FW download speed */
		KEEP(*(.lowpower_ram2))
		. = ALIGN(4);
		__flash_lplfw_end = .;
#endif /* CONFIG_EXTERNAL_STORAGE */
	} > EC_IMAGE_VMA_MEM_REGION AT > EC_IMAGE_LMA_MEM_REGION

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

		__usb_desc = .;
		KEEP(*(.rodata.usb_desc_conf))
		KEEP(*(SORT(.rodata.usb_desc*)))
		__usb_desc_end = .;
		. = ALIGN(4);
		KEEP(*(.rodata.usb_ep))
		KEEP(*(.rodata.usb_ep.usb_ep_tx))
		KEEP(*(.rodata.usb_ep.usb_ep_rx))
		KEEP(*(.rodata.usb_ep.usb_ep_reset))
		KEEP(*(.rodata.usb_ep.usb_iface_request))

		. = ALIGN(4);
		*(.rodata*)

#ifndef CONFIG_CHIP_INIT_ROM_REGION
		/*
		 * When a separate ROM resident section isn't enabled, ensure
		 * the corresponding data objects are linked into the .rodata
		 * section.
		 */
		. = ALIGN(4);
		__init_rom_start = .;
		*(.init.rom)
		__init_rom_end = .;
#endif /* CONFIG_CHIP_INIT_ROM_REGION */

#if defined(SECTION_IS_RO) && defined(CONFIG_FLASH_CROS)
		. = ALIGN(64);
		KEEP(*(.google))
#endif

		/*
		 * https://developer.arm.com/documentation/dui0475/c/the-arm-c-and-c---libraries/c---initialization--construction-and-destruction
		 */
		. = ALIGN(4);
		__init_array_start = .;
		*(.init_array*)
		__init_array_end = .;

		/*
		 * Empty C++ exception index table. The exception index
		 * table is described in more detail in "Exception Handling
		 * ABI for ARM Architecture":
		 * https://github.com/ARM-software/abi-aa/blob/60a8eb8c55e999d74dac5e368fc9d7e36e38dda4/ehabi32/ehabi32.rst#54the-object-producer-interface
		 */
		__exidx_start = 0;
		__exidx_end = 0;

		. = ALIGN(4);
	} > EC_IMAGE_VMA_MEM_REGION AT > EC_IMAGE_LMA_MEM_REGION

#ifdef CONFIG_CHIP_DATA_IN_INIT_ROM
	__data_lma_start = ORIGIN(ROM_RESIDENT_VMA);
	#define INIT_ROM_LMA	(ORIGIN(ROM_RESIDENT_VMA) + SIZEOF(.data))
#else
/*
 * TODO(b/202863352): Unify clang and gcc behavior.
 */
#ifdef __clang__
#if defined(SECTION_IS_RO) && defined(NPCX_RO_HEADER)
	__data_lma_start = LOADADDR(.data) - SIZEOF(.header);
#else
	__data_lma_start = LOADADDR(.data);
#endif
#else
	__data_lma_start = .;
#endif
	#define INIT_ROM_LMA	ORIGIN(ROM_RESIDENT_VMA)
#endif

	/*
	 * This SRAM section is not used by common/ or chip/stm32/, but
	 * available if particular boards want to move the vector table
	 * into SRAM for performance or mutability.
	 */
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
		. = ALIGN(8);
		*(SORT(.preserved_logs.*))
		. = ALIGN(8);
		__preserved_logs_end = .;
	} > IRAM

	ASSERT((SIZEOF(.preserve_logs) + CONFIG_RAM_BASE) ==
	       __preserved_logs_end,
	       "preserve_logs must be at CONFIG_RAM_BASE.")
#endif

	.bss : {
		/*
		 * Align to 512 bytes. This is convenient when some memory block
		 * needs big alignment. This is the beginning of the RAM,
		 * so there is usually no penalty on aligning this.
		 */
		. = ALIGN(512);
		__bss_start = .;
		*(.bss.big_align)
		/* Stacks must be 64-bit aligned */
		. = ALIGN(8);
		*(.bss.system_stack)
		/* Rest of .bss takes care of its own alignment */

		/* Group libtpm2 data so it can be cleared on system reset */
		__bss_libtpm2_start = .;
		/* TPM registers should be cleared at the same time */
		STRINGIFY(OUTDIR/common/tpm_registers.o*)(.bss)
			*(.bss.Tpm2_common)
		__bss_libtpm2_end = .;

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

	.data : {
		. = ALIGN(4);
		__data_start = .;
		*(.data.tasks)

		/*
		 * Group libtpm2 data so it can be reinitialized on
		 * system reset
		 */
		__data_libtpm2_start = .;
		Tpm2_*(.data)
		/* TPM registers should be reinitialized at the same time */
		STRINGIFY(OUTDIR/common/tpm_registers.o*)(.data)
		__data_libtpm2_end = .;

		/*
		 * TPM reset currently only clears BSS for the TPM library.
		 * It does not reset any initialized variables in data.
		 * So, make sure there aren't any.
		 */
		ASSERT(__data_libtpm2_start == __data_libtpm2_end,
		       "libtpm2 .data section is nonzero");

		*(.data*)
#ifdef CONFIG_MPU
		/*
		 * It has to be aligned by 32 bytes to be a valid
		 * MPU region.
		 */
		. = ALIGN(32);
		__iram_text_start = .;
#else
		. = ALIGN(4);
#endif
		*(.iram.text)
#ifdef CONFIG_MPU
		. = ALIGN(32);
		__iram_text_end = .;
#else
		. = ALIGN(4);
#endif
		__data_end = .;

		/*
		 * Shared memory buffer must be at the end of preallocated
		 * RAM, so it can expand to use all the remaining RAM.
		 */
		__shared_mem_buf = .;

		/* NOTHING MAY GO AFTER THIS! */
	} > IRAM AT > DATA_LMA_MEM_REGION

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

#ifdef CONFIG_CHIP_DATA_IN_INIT_ROM
	/*
	 * .data is ROM resident, last section in the EC image is the .rodata
	 * section.
	 */
	#define FLASH_USED_END	(LOADADDR(.rodata) + SIZEOF(.rodata))
#else
	/*
	 * .data is included in the EC image and copied to RAM by the loader.
	 */
	#define FLASH_USED_END	(LOADADDR(.data) + SIZEOF(.data))
#endif

	/*
	 * __flash_used and __flash_size is used in flash free calculations by
	 * the makefile. Note the difference between __flash_size and
	 * __config_flash_size. __flash_size is the flash size for the given
	 * image (RW, RO, etc.), while __config_flash_size is the *total*
	 * flash size.
	 * __image_size is stored in the struct image_data header and used
	 * in hash calculations.
	 */
	__flash_used = FLASH_USED_END - ORIGIN(EC_IMAGE_LMA_MEM_REGION);
	__flash_size = LENGTH(EC_IMAGE_LMA_MEM_REGION);
#ifndef CONFIG_CHIP_INIT_ROM_REGION
#if defined(SECTION_IS_RW) && (CONFIG_FLASH_WRITE_SIZE > 4)
	.rw_image_size_alignment FLASH_USED_END : AT(FLASH_USED_END)
	{
		. = ALIGN (CONFIG_FLASH_WRITE_SIZE);
	} > FLASH = 0xFF

	__image_size = __flash_used + SIZEOF(.rw_image_size_alignment);
#else
	__image_size = __flash_used;
#endif
#if defined(SECTION_IS_RW) && (CONFIG_FLASH_WRITE_SIZE > 0)
	/* Alignment is needed by flash_command_write() for RW update. */
	ASSERT(__image_size % CONFIG_FLASH_WRITE_SIZE == 0,
	       "Error: RW image is not aligned to CONFIG_FLASH_WRITE_SIZE")
#endif
#endif /* CONFIG_CHIP_INIT_ROM_REGION */

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
	ASSERT((LENGTH(EC_IMAGE_LMA_MEM_REGION)
#if defined(CONFIG_RWSIG) && defined(SECTION_IS_RO)
	       - CONFIG_RO_PUBKEY_SIZE
#endif
#if defined(CONFIG_RWSIG) && defined(SECTION_IS_RW)
	       - CONFIG_RW_SIG_SIZE
#endif
	       ) >= __flash_used,
	       "No room left in the flash")

#ifdef CONFIG_CHIP_INIT_ROM_REGION
	/*
	 * Image layout when ROM_RESIDENT region is used (lower addresses
	 * at the top). This layout is setup by the LMA assignment.
	 *
	 *	EC image layout (LMA)		VMA
	 *	.header (if RO image)		none
	 *	.text				code RAM
	 *	.rodata				code RAM + .text size
	 *	.data				data RAM
	 *	.fill				none
	 *	.init_rom			flash offset
	 *
	 * The loader code copies the .text, .rodata, and .data sections into
	 * the code RAM of the EC.  The .header and .init_rom sections are not
	 * copied by the loader.
	 *
	 * Image layout when ROM_RESIDENT region is used, and
	 * CONFIG_CHIP_DATA_IN_INIT_ROM is enabled.
	 *
	 *	EC image layout (LMA)		VMA
	 *	.header (if RO image)		none
	 *	.text				code RAM
	 *	.rodata				code RAM + .text size
	 *	.fill				none
	 *	.data				data RAM
	 *	.init_rom			flash offset
	 *
	 * The loader code copies the .text and .rodata sections into the code
	 * RAM of the EC.  The .header, .data, and .init_rom sections are not
	 * copied by the loader.
	 *
	 * EC initialization code copies the .data directly from flash to
	 * data RAM at runtime.
	 */

	/*
	 * The layout assumes the ROM_RESIDENT region follows the FLASH
	 * region.
	 */
	ASSERT((ORIGIN(FLASH) + LENGTH(FLASH)) == ORIGIN(ROM_RESIDENT),
		".init_rom section must follow the flash section")

	.init_rom INIT_ROM_LMA : {
		. = ALIGN(4);
		__init_rom_start = .;
		*(.init.rom)
		__init_rom_end = .;
	} > ROM_RESIDENT_VMA AT > ROM_RESIDENT

	/*
	 * The ROM_RESIDENT section is assumed to be in the same physical
	 * flash as the FLASH section. Fill the space between.
	 */
	.fill : {
		. = FLASH_USED_END;
		. = ALIGN(4);
		__fill_start = .;
		FILL(0xFF);
		. = ORIGIN(FLASH) + LENGTH(FLASH) - 1;
		/* Need at least one byte so section is not omitted */
		BYTE(0xFF);
		__fill_end = .;
	} > FLASH

	/*
	 * The end of the .fill region should also be the start of the ROM
	 * resident region.
	 */
	ASSERT(__fill_end == ORIGIN(ROM_RESIDENT),
		".fill region end not aligned to start of ROM_RESIDENT region")

	/*
	 * __image_size is used for hash calculation.  When
	 * CONFIG_CHIP_INIT_ROM_REGION is enabled, this includes the entire
	 * FLASH region and the bytes used in the .init_rom section.
	 */
#ifdef CONFIG_CHIP_DATA_IN_INIT_ROM
	__image_size = LENGTH(FLASH) + SIZEOF(.init_rom) + SIZEOF(.data);
#else
	__image_size = LENGTH(FLASH) + SIZEOF(.init_rom);
#endif /* CONFIG_CHIP_DATA_IN_INIT_ROM */
#endif /* CONFIG_CHIP_INIT_ROM_REGION */

#ifdef CONFIG_CHIP_MEMORY_REGIONS
#define REGION(name, attr, start, size) \
	.name(NOLOAD) : { \
		__##name##_start = .; \
		KEEP(*(SORT(.name.keep.*))) \
		*(SORT(.name.*)) \
	} > name
#define REGION_LOAD(name, attr, start, size) \
	.name : { \
		__##name##_start = .; \
		KEEP(*(SORT(.name.keep.*))) \
		*(SORT(.name.*)) \
	} > name
#include "memory_regions.inc"
#undef REGION
#undef REGION_LOAD
#endif /* CONFIG_CHIP_MEMORY_REGIONS */

#ifdef CONFIG_DRAM_BASE

	/*
	 * Sections in DRAM region are constructed as like in non-DRAM regions:
	 * .dram.data LMA is for preserving initialized data across resets.
	 * The only difference is that they are all in the DRAM region:
	 * .dram.text     | LOAD
	 * .dram.rodata   | LOAD
	 * .dram.data LMA | LOAD
	 * .dram.data VMA |
	 * .dram.bss      | NOLOAD
	 * TODO(b:123269246): Enable MPU protectable DRAM section.  This might
	 * introduce a RO-DRAM section for .dram.text, .dram.rodata and
	 * .dram.data LMA.
	 */

	.dram.text : {
		. = ALIGN(4);
		KEEP(*(SORT(.dram.text.keep.*)))
		*(SORT(.dram.text.*))
		. = ALIGN(4);
	} > DRAM

	.dram.rodata : {
		. = ALIGN(4);
		KEEP(*(SORT(.dram.rodata.keep.*)))
		*(SORT(.dram.rodata.*))
		. = ALIGN(4);
	} > DRAM

	__dram_data_lma_start = ADDR(.dram.rodata) + SIZEOF(.dram.rodata);

	/* Place .dram.data LMA in between .dram.rodata and .dram.data VMA. */
#ifdef __clang__
	/*
	 * The evaluation timing for SIZEOF() and symbols are different in
	 * ld and lld.
	 */
	.dram.data __dram_data_lma_start + SIZEOF(.dram.data) : {
#else
	.dram.data __dram_data_lma_start +
		(__dram_data_end - __dram_data_start) : {
#endif /* __clang__ */
		. = ALIGN(4);
		__dram_data_start = .;
		*(.dram.data*)
		. = ALIGN(4);
		__dram_data_end = .;

	/*
	 * Normally, '> DRAM AT > DRAM' should be the same as '> DRAM',
	 * and they  will be at the same address. However, if the address
	 * of VMA specified, LMA and VMA might have different addresses:
	 * '> DRAM' places VMA at the address where section declaration
	 * specified.
	 * 'AT > DRAM' places LMA at the location counter's address.
	 */
	} > DRAM AT > DRAM

	/*
	 * ld assigns correct attribute for .bss, but not for other .*.bss,
	 * we need an explicltly NOLOAD.
	 */
	.dram.bss(NOLOAD) : {
		. = ALIGN(4);
		__dram_bss_start = .;
		*(SORT(.dram.bss*))
		. = ALIGN(4);
		__dram_bss_end = .;
	} > DRAM
#endif

#if !(defined(SECTION_IS_RO) && defined(CONFIG_FLASH_CROS))
	/DISCARD/ : { *(.google) }
#endif
	/DISCARD/ : { *(.ARM.*) }
}
