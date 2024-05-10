/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"

#define STRINGIFY0(name) #name
#define STRINGIFY(name) STRINGIFY0(name)

#define FW_OFF_(section) CONFIG_##section##_MEM_OFF
#define FW_OFF(section) (CONFIG_PROGRAM_MEMORY_BASE + FW_OFF_(section))

#define FW_SIZE_(section) CONFIG_##section##_SIZE
#define FW_SIZE(section) FW_SIZE_(section)

OUTPUT_FORMAT(BFD_FORMAT, BFD_FORMAT, BFD_FORMAT)
OUTPUT_ARCH(BFD_ARCH)
ENTRY(__reset)

MEMORY
{
#if defined(CONFIG_FLASH_PHYSICAL)
	FLASH (rx) : ORIGIN = FW_OFF(SECTION) - CHIP_ILM_BASE, \
		     LENGTH = FW_SIZE(SECTION)
#else
	IROM (rx) : ORIGIN = CONFIG_ROM_BASE, LENGTH = CONFIG_ROM_SIZE
#endif

#if defined(CHIP_FAMILY_IT8XXX2)
	/*
	 * On IT8XXX2 family, it reserves space for ramcode, h2ram, and
	 * immu sections.
	 */
	IRAM (rw) : ORIGIN = CONFIG_RAM_BASE + CHIP_RAM_SPACE_RESERVED,
		    LENGTH = CONFIG_RAM_SIZE - CHIP_RAM_SPACE_RESERVED
	/*
	 * ILM (Instruction Local Memory).
	 * We connect ILM to internal flash so we are able to
	 * boot from the flash.
	 */
	ILM (rx) : ORIGIN = FW_OFF(SECTION), LENGTH = FW_SIZE(SECTION)

#if defined(CONFIG_HOSTCMD_X86) || defined(CONFIG_I2C_PERIPHERAL)
	H2RAM (rw) : ORIGIN = CONFIG_H2RAM_BASE, LENGTH = CONFIG_H2RAM_SIZE
#endif
#else
	IRAM (rw) : ORIGIN = CONFIG_RAM_BASE, LENGTH = CONFIG_RAM_SIZE
#endif /* CHIP_FAMILY_IT8XXX2 */

#ifdef CONFIG_DRAM_BASE
	DRAM (rwx) : ORIGIN = CONFIG_DRAM_BASE, LENGTH = CONFIG_DRAM_SIZE
#endif

#if defined(CHIP_VARIANT_MT8195) && defined(CONFIG_CHIP_MEMORY_REGIONS)
#define REGION(name, attr, start, size) \
	name(attr) : ORIGIN = (start), LENGTH = (size)
#define REGION_LOAD REGION
#include "memory_regions.inc"
#undef REGION
#undef REGION_LOAD
#endif /* CONFIG_MEMORY_REGIONS */
}

SECTIONS
{
	.text : {
#if defined(CHIP_FAMILY_IT8XXX2)
		/*
		 * We put "__flash_dma_start" at the beginning of the
		 * text section to avoid gap.
		 */
		__flash_dma_start = .;
		ASSERT((__flash_dma_start == 0),
		       "__flash_dma_start has to be 4k-byte aligned");
#endif
		KEEP(STRINGIFY(OUTDIR/core/CORE/init.o) (.text.vecttable))
		. = ALIGN(4);
		__image_data_offset = .;
		KEEP(*(.rodata.ver))

		. = ALIGN(4);
		KEEP(STRINGIFY(OUTDIR/core/CORE/init.o) (.text.vectirq))
		KEEP(STRINGIFY(OUTDIR/core/CORE/init.o) (.text))
#if defined(CHIP_FAMILY_IT8XXX2)
		KEEP(*(.flash_direct_map))
		. = ALIGN(16);
		KEEP(*(.ram_code))

		__flash_dma_size = . - __flash_dma_start;
		ASSERT((__flash_dma_size < IT83XX_ILM_BLOCK_SIZE),
		       "__flash_dma_size < IT83XX_ILM_BLOCK_SIZE");
		. = ALIGN(IT83XX_ILM_BLOCK_SIZE);
		__ilm0_ram_code = .;
		KEEP(*(.ram_code_ilm0))
		OUTDIR/chip/it83xx/i2c.o (.text*)
		OUTDIR/chip/it83xx/i2c.o (.rodata)
		OUTDIR/chip/it83xx/hwtimer.o (.text*)
		OUTDIR/chip/it83xx/hwtimer.o (.rodata)
		. = ALIGN(16);
#endif
#if SCP_CORE_SN == 1
		EXCLUDE_FILE (STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/cam*.o) STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/img*.o) STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/libmt_imgsys.a:*.o)) *(.text*)
#else
		*(.text*)
#endif
		. = ALIGN(4);
#if defined(CONFIG_FLASH_PHYSICAL)
#	if defined(CHIP_FAMILY_IT8XXX2)
	} > ILM AT > FLASH
#	else
	} > FLASH
#	endif
#else
	} > IROM
#endif /* CONFIG_FLASH_PHYSICAL */

	. = ALIGN(4);
	.rodata : {
		/* Symbols defined here are declared in link_defs.h */
		__irqprio = .;
		KEEP(*(.rodata.irqprio))
		__irqprio_end = .;

		. = ALIGN(4);
		__irqhandler = .;
		KEEP(STRINGIFY(OUTDIR/core/CORE/init.o) (.rodata.vecthandlers))

		. = ALIGN(4);
		__cmds = .;
		KEEP(*(SORT(.rodata.cmds*)))
		__cmds_end = .;

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

		. = ALIGN(4);

#if SCP_CORE_SN == 1
		EXCLUDE_FILE (STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/cam*.o) STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/img*.o) STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/libmt_imgsys.a:*.o)) *(.rodata*)
#else
		*(.rodata*)
#endif

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

		. = ALIGN(4);
		*(.srodata*)
#if defined(SECTION_IS_RO) && defined(CONFIG_FLASH_CROS)
		. = ALIGN(64);
		KEEP(*(.google))
#endif
		. = ALIGN(4);
#if defined(CONFIG_FLASH_PHYSICAL)
#	if defined(CHIP_FAMILY_IT8XXX2)
	} > ILM AT > FLASH
#	else
	} > FLASH
#	endif
#else
	} > IROM
#endif /* CONFIG_FLASH_PHYSICAL */

	__data_lma_start = .;

#ifdef CONFIG_PRESERVE_LOGS
	.preserve_logs(NOLOAD) : {
		/*
		 * The address of the preserved logs is fixed at the beginning
		 * of memory.
		 */
		. = ALIGN(8);
		__preserved_logs_start = .;
		*(SORT(.preserved_logs.*))
		. = ALIGN(8);
		__preserved_logs_end = .;
	} > IRAM
	__preserved_logs_size = __preserved_logs_end - __preserved_logs_start;
#ifdef CONFIG_IT83XX_HARD_RESET_BY_GPG1
	ASSERT(__preserved_logs_size <= CHIP_FLASH_PRESERVE_LOGS_SIZE,
	       "Not enough flash space to save EC logs.")
#endif
#endif /* CONFIG_PRESERVE_LOGS */

	.data : {
		. = ALIGN(4);
		__data_start = .;
#if SCP_CORE_SN == 1
		EXCLUDE_FILE (STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/cam*.o) STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/img*.o) STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/libmt_imgsys.a:*.o)) *(.data)
#else
		*(.data)
#endif
		*(.sdata)
		. = ALIGN(4);
		__data_end = .;
#if defined(CONFIG_FLASH_PHYSICAL)
	} > IRAM AT > FLASH
#else
	} > IRAM AT > IROM
#endif

	.bss : {
		/* Stacks must be 128-bit aligned */
		. = ALIGN(16);
		__bss_start = .;
		*(.bss.tasks)
		. = ALIGN(16);
		*(.bss.system_stack)
		. = ALIGN(16);
		*(.bss.task_scratchpad)
		. = ALIGN(16);
		__global_pointer$ = .;
		*(.sbss)
		. = ALIGN(4);
		/* Rest of .bss takes care of its own alignment */
#if SCP_CORE_SN == 1
		EXCLUDE_FILE (STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/cam*.o) STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/img*.o) STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/libmt_imgsys.a:*.o)) *(.bss)
#else
		*(.bss)
#endif

		/*
		 * Reserve space for deferred function firing times.
		 * Each time is a uint64_t, each func is a 32-bit pointer,
		 * thus the scaling factor of two.
		 */
		. = ALIGN(8);
		__deferred_until = .;
		. += (__deferred_funcs_end - __deferred_funcs) * (8 / 4);
		__deferred_until_end = .;

		. = ALIGN(4);
		__bss_end = .;

		/*
		 * Shared memory buffer must be at the end of preallocated RAM,
		 * so it can expand to use all the remaining RAM.
		 */
		__shared_mem_buf = .;
	} > IRAM

	ASSERT((__shared_mem_buf + CONFIG_SHAREDMEM_MINIMUM_SIZE) <=
	       (CONFIG_RAM_BASE + CONFIG_RAM_SIZE),
	       "Not enough space for shared memory.")

	__ram_free = (CONFIG_RAM_BASE + CONFIG_RAM_SIZE) -
		     (__shared_mem_buf + CONFIG_SHAREDMEM_MINIMUM_SIZE);

	/*
	 * __flash_used and __flash_size is used in flash free calculations by
	 * the makefile. Note the difference between __flash_size and
	 * __config_flash_size. __flash_size is the flash size for the given
	 * image (RW, RO, etc.), while __config_flash_size is the *total*
	 * flash size.
	 * __image_size is stored in the struct image_data header and used
	 * in hash calculations.
	 */
#if defined(CHIP_FAMILY_IT8XXX2)
	__flash_used = LOADADDR(.data) + SIZEOF(.data) + \
		       CHIP_ILM_BASE - FW_OFF(SECTION);
#else
	__flash_used = LOADADDR(.data) + SIZEOF(.data) - FW_OFF(SECTION);
#endif
#if defined(CONFIG_FLASH_PHYSICAL)
	__flash_size = LENGTH(FLASH);
#else
	__flash_size = LENGTH(IROM);
#endif
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

#if defined(CHIP_FAMILY_IT8XXX2)
#if defined(CONFIG_HOSTCMD_X86) || defined(CONFIG_I2C_PERIPHERAL)
	.h2ram (NOLOAD) : {
		. += CONFIG_H2RAM_HOST_LPC_IO_BASE;
		*(.h2ram.pool.hostcmd)
		. = ALIGN(256);
		*(.h2ram.pool.acpiec)
#ifdef CONFIG_I2C_PERIPHERAL
		. = ALIGN(256);
		*(.h2ram.pool.i2cslv)
#endif
		__h2ram_end = .;
	} > H2RAM

	ASSERT((__h2ram_end) <= (CONFIG_H2RAM_BASE + CONFIG_H2RAM_SIZE),
	       "Not enough space for h2ram section.")
#endif
#endif /* CHIP_FAMILY_IT8XXX2 */

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
	 */

	.dram.text : {
		. = ALIGN(4);
		KEEP(*(SORT(.dram.text.keep.*)))
		*(SORT(.dram.text.*))
#if SCP_CORE_SN == 1
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/libmt_imgsys.a:*.o) (.text*)
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/cam*.o) (.text*)
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/img*.o) (.text*)
#endif
		. = ALIGN(4);
	} > DRAM

	.dram.rodata : {
		. = ALIGN(4);
		KEEP(*(SORT(.dram.rodata.keep.*)))
		*(SORT(.dram.rodata.*))
#if SCP_CORE_SN == 1
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/libmt_imgsys.a:*.o) (.rodata*)
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/cam*.o) (.rodata*)
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/img*.o) (.rodata*)
#endif
		. = ALIGN(4);
	} > DRAM

	__dram_data_lma_start = ADDR(.dram.rodata) + SIZEOF(.dram.rodata);

	/* Place .dram.data LMA in between .dram.rodata and .dram.data VMA. */
	.dram.data __dram_data_lma_start +
		(__dram_data_end - __dram_data_start) : {
		. = ALIGN(4);
		__dram_data_start = .;
		*(.dram.data*)
#if SCP_CORE_SN == 1
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/libmt_imgsys.a:*.o) (.data)
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/cam*.o) (.data)
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/img*.o) (.data)
#endif
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
#if SCP_CORE_SN == 1
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/libmt_imgsys.a:*.o) (.bss)
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/cam*.o) (.bss)
		STRINGIFY(OUTDIR/private-mt-scp/board/BOARD/img*.o) (.bss)
#endif
		. = ALIGN(4);
		__dram_bss_end = .;
	} > DRAM
#endif /* CONFIG_DRAM_BASE */

#if defined(CHIP_VARIANT_MT8195) && defined(CONFIG_CHIP_MEMORY_REGIONS)
#define REGION(name, attr, start, size) \
	.name(NOLOAD) : { \
		. = ALIGN(4); \
		__##name##_start = .; \
		KEEP(*(SORT(.name.keep.*))) \
		*(SORT(.name.*)) \
		. = ALIGN(4); \
		__##name##_end = .; \
	} > name
#define REGION_LOAD(name, attr, start, size) \
	.name : { \
		. = ALIGN(4); \
		__##name##_start = .; \
		KEEP(*(SORT(.name.keep.*))) \
		*(SORT(.name.*)) \
		. = ALIGN(4); \
		__##name##_end = .; \
	} > name
#include "memory_regions.inc"
#undef REGION
#undef REGION_LOAD
#endif /* CONFIG_CHIP_MEMORY_REGIONS */

#if !(defined(SECTION_IS_RO) && defined(CONFIG_FLASH_CROS))
	/DISCARD/ : { *(.google) }
#endif

	/DISCARD/ : { *(.ARM.*) }
}
