/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "config.h"
#include "rwsig.h"

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
                 LENGTH = FW_SIZE(SECTION)
#else
    FLASH (rx) : ORIGIN = FW_OFF(SECTION), LENGTH = FW_SIZE(SECTION)
#endif
#ifdef CONFIG_SHAREDLIB
    SHARED_LIB (rx) : ORIGIN = FW_OFF(SHAREDLIB), LENGTH = FW_SIZE(SHAREDLIB)
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
#ifdef SECTION_IS_RO
	. = . +  CONFIG_RO_HEAD_ROOM;
#endif
#ifdef SECTION_IS_RW
	. = . +  CONFIG_RW_HEAD_ROOM;
#endif
        *(.text.vecttable)
        . = ALIGN(4);
        __image_data_offset = .;
        KEEP(*(.rodata.ver))

        . = ALIGN(4);
        KEEP(*(.rodata.pstate))

        . = ALIGN(4);
        STRINGIFY(OUTDIR/core/CORE/init.o) (.text)
#if defined(CHIP_FAMILY_NPCX7) && !defined(CONFIG_HIBERNATE_PSL)
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
    } > CDRAM AT > FLASH
#else
#if !defined(CONFIG_FLASH_PHYSICAL)
    } > IROM
#else
    } > FLASH
#endif /* !CONFIG_FLASH_PHYSICAL */
#endif
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

        __hooks_base_attached_change = .;
        KEEP(*(.rodata.HOOK_BASE_ATTACHED_CHANGE))
        __hooks_base_attached_change_end = .;

        __hooks_pwrbtn_change = .;
        KEEP(*(.rodata.HOOK_POWER_BUTTON_CHANGE))
        __hooks_pwrbtn_change_end = .;

        __hooks_battery_soc_change = .;
        KEEP(*(.rodata.HOOK_BATTERY_SOC_CHANGE))
        __hooks_battery_soc_change_end = .;

#ifdef CONFIG_CASE_CLOSED_DEBUG_V1
        __hooks_ccd_change = .;
        KEEP(*(.rodata.HOOK_CCD_CHANGE))
        __hooks_ccd_change_end = .;
#endif

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

#if defined(SECTION_IS_RO) && defined(CONFIG_FLASH)
        . = ALIGN(64);
        KEEP(*(.google))
#endif
        . = ALIGN(4);
#if !defined(CONFIG_FLASH_PHYSICAL)
    } > IROM
#define DATA_LMA_MEM_REGION IROM
#elif defined(CONFIG_EXTERNAL_STORAGE)
    } > CDRAM AT > FLASH
#define DATA_LMA_MEM_REGION FLASH
#else
    } > FLASH
#define DATA_LMA_MEM_REGION FLASH
#endif

    __data_lma_start = .;

    .bss : {
	/*
	 * Align to 512 bytes. This is convenient when some memory block
	 * needs big alignment. This is the beginning of the RAM, so there
	 * is usually no penalty on aligning this.
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

        *(.bss)

	/*
	 * Reserve space for deferred function firing times.  Each time is a
	 * uint64_t, each func is a 32-bit pointer, thus the scaling factor of
	 * two.  The 8 byte alignment of uint64_t is required by the ARM ABI.
	 */
        . = ALIGN(8);
        __deferred_until = .;
	. += (__deferred_funcs_end - __deferred_funcs) * (8 / 4);
        __deferred_until_end = .;

    } > IRAM

    .bss.slow : {
	/* Region of RAM reclaimed from the little firmware(LFW). */
	*(.bss.slow)
	/*
	 * Not replacing the loader, so .bss.slow is part of .bss.  It needs to
	 * be followed by __bss_end so that .bss.slow will be zeroed by init.
	 */
        . = ALIGN(4);
        __bss_end = .;
    } > IRAM

    .data : {
        . = ALIGN(4);
        __data_start = .;
        *(.data.tasks)

	/* Group libtpm2 data so it can be reinitialized on system reset */
	__data_libtpm2_start = .;
	Tpm2_*(.data)
	/* TPM registers should be reinitialized at the same time */
        STRINGIFY(OUTDIR/common/tpm_registers.o*)(.data)
	__data_libtpm2_end = .;

	/*
	 * TPM reset currently only clears BSS for the TPM library.  It does
	 * not reset any initialized variables in data.  So, make sure there
	 * aren't any.
	 */
	ASSERT(__data_libtpm2_start == __data_libtpm2_end,
        "libtpm2 .data section is nonzero");

        *(.data*)
#ifdef CONFIG_MPU
        /* It has to be aligned by 32 bytes to be a valid MPU region. */
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

	/* Shared memory buffer must be at the end of preallocated RAM, so it
	 * can expand to use all the remaining RAM. */
	__shared_mem_buf = .;

	/* NOTHING MAY GO AFTER THIS! */
    } > IRAM AT > DATA_LMA_MEM_REGION

    ASSERT((__shared_mem_buf + CONFIG_SHAREDMEM_MINIMUM_SIZE) <=
               (CONFIG_RAM_BASE + CONFIG_RAM_SIZE),
           "Not enough space for shared memory.")
    __ram_free = (CONFIG_RAM_BASE + CONFIG_RAM_SIZE) -
        (__shared_mem_buf + CONFIG_SHAREDMEM_MINIMUM_SIZE);

    /* The linker won't notice if the .data section is too big to fit,
     * apparently because we're sending it into IRAM, not FLASH. The following
     * symbol isn't used by the code, but running "objdump -t *.elf | grep hey"
     * will let us check how much flash space we're actually using. The
     * explicit ASSERT afterwards will cause the linker to abort if we use too
     * much. */
    __hey_flash_used = LOADADDR(.data) + SIZEOF(.data) - FW_OFF(SECTION);

#ifdef CONFIG_FLASH_PHYSICAL
    ASSERT((FW_SIZE(SECTION)
#if defined(CONFIG_RWSIG) && defined(SECTION_IS_RO)
        - CONFIG_RO_PUBKEY_SIZE
#endif
#if defined(CONFIG_RWSIG) && defined(SECTION_IS_RW)
        - CONFIG_RW_SIG_SIZE
#endif
        ) >= (LOADADDR(.data) + SIZEOF(.data) - FW_OFF(SECTION)),
                 "No room left in the flash")
#endif /* CONFIG_FLASH_PHYSICAL */

#if defined(SECTION_IS_RO) && defined(NPCX_RO_HEADER)
    __image_size = __hey_flash_used - FW_SIZE(RO_HDR);
#else
   __image_size = __hey_flash_used;
#endif

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
    .dram.data __dram_data_lma_start + (__dram_data_end - __dram_data_start) : {
#endif /* __clang__ */
        . = ALIGN(4);
        __dram_data_start = .;
        *(.dram.data*)
        . = ALIGN(4);
        __dram_data_end = .;
    /*
     * Normally, '> DRAM AT > DRAM' should be the same as '> DRAM', and they
     * will be at the same address.  However, if the address of VMA specified,
     * LMA and VMA might have different addresses:
     * '> DRAM' places VMA at the address where section declaration specified.
     * 'AT > DRAM' places LMA at the location counter's address.
     */
    } > DRAM AT > DRAM

    /*
     * ld assigns correct attribute for .bss, but not for other .*.bss, we need
     * an explicltly NOLOAD.
     */
    .dram.bss(NOLOAD) : {
        . = ALIGN(4);
        __dram_bss_start = .;
        *(SORT(.dram.bss*))
        . = ALIGN(4);
        __dram_bss_end = .;
    } > DRAM
#endif

#if !(defined(SECTION_IS_RO) && defined(CONFIG_FLASH))
    /DISCARD/ : {
              *(.google)
              }
#endif

    /DISCARD/ : { *(.ARM.*) }
}
