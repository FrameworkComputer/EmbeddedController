/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "config.h"

#define FW_OFF_(section) CONFIG_FW_##section##_OFF
#define FW_OFF(section) (CONFIG_FLASH_BASE + FW_OFF_(section))

#define FW_SIZE_(section) CONFIG_FW_##section##_SIZE
#define FW_SIZE(section) FW_SIZE_(section)


OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(reset)
MEMORY
{
    FLASH (rx) : ORIGIN = FW_OFF(SECTION), LENGTH = FW_SIZE(SECTION)
    IRAM (rw)  : ORIGIN = CONFIG_RAM_BASE, LENGTH = CONFIG_RAM_SIZE
}
SECTIONS
{
    .text : {
        OUTDIR/core/CORE/init.o (.text.vecttable)
        . = ALIGN(4);
        __version_struct_offset = .;
        *(.rodata.ver)
#ifdef SHIFT_CODE_FOR_TEST
        . = ALIGN(256);
#else
        . = ALIGN(4);
#endif
        OUTDIR/core/CORE/init.o (.text)
        *(.text*)
#ifdef COMPILE_FOR_RAM
    } > IRAM
#else
    } > FLASH
#endif
    . = ALIGN(4);
    .rodata : {
	/* Symbols defined here are declared in link_defs.h */
        __irqprio = .;
        *(.rodata.irqprio)
        __irqprio_end = .;

        . = ALIGN(4);
        __cmds = .;
        *(SORT(.rodata.cmds*))
        __cmds_end = .;

        . = ALIGN(4);
        __hcmds = .;
        *(.rodata.hcmds)
        __hcmds_end = .;

        . = ALIGN(4);
        __hooks_init = .;
        *(.rodata.HOOK_INIT)
        __hooks_init_end = .;

        __hooks_freq_change = .;
        *(.rodata.HOOK_FREQ_CHANGE)
        __hooks_freq_change_end = .;

        __hooks_sysjump = .;
        *(.rodata.HOOK_SYSJUMP)
        __hooks_sysjump_end = .;

        __hooks_chipset_startup = .;
        *(.rodata.HOOK_CHIPSET_STARTUP)
        __hooks_chipset_startup_end = .;

        __hooks_chipset_resume = .;
        *(.rodata.HOOK_CHIPSET_RESUME)
        __hooks_chipset_resume_end = .;

        __hooks_chipset_suspend = .;
        *(.rodata.HOOK_CHIPSET_SUSPEND)
        __hooks_chipset_suspend_end = .;

        __hooks_chipset_shutdown = .;
        *(.rodata.HOOK_CHIPSET_SHUTDOWN)
        __hooks_chipset_shutdown_end = .;

        __hooks_ac_change = .;
        *(.rodata.HOOK_AC_CHANGE)
        __hooks_ac_change_end = .;

        __hooks_lid_change = .;
        *(.rodata.HOOK_LID_CHANGE)
        __hooks_lid_change_end = .;

        . = ALIGN(4);
        *(.rodata*)

#if defined(SECTION_IS_RO) && defined(CONFIG_FLASH)
        . = ALIGN(64);
        *(.google)
#endif
        . = ALIGN(4);
#ifdef COMPILE_FOR_RAM
    } > IRAM
#else
    } > FLASH
#endif
    __ro_end = . ;
    .bss : {
	/* Stacks must be 64-bit aligned */
        . = ALIGN(8);
        __bss_start = .;
        *(.bss.tasks)
        *(.bss.task_scratchpad)
        . = ALIGN(8);
        *(.bss.system_stack)
	/* Rest of .bss takes care of its own alignment */
        *(.bss)
        . = ALIGN(4);
        __bss_end = .;
    } > IRAM
#ifdef COMPILE_FOR_RAM
    .data : {
#else
    .data : AT(ADDR(.rodata) + SIZEOF(.rodata)) {
#endif
        . = ALIGN(4);
        __data_start = .;
        *(.data.tasks)
        *(.data)
        . = ALIGN(4);
        *(.iram.text)
        . = ALIGN(4);
        __data_end = .;

	/* Shared memory buffer must be at the end of preallocated RAM, so it
	 * can expand to use all the remaining RAM. */
	__shared_mem_buf = .;

	/* Tag at end of firmware image so that we can find the image size.
	 * This may be overwritten by the shared memory buffer; that's ok
	 * because we only use it to find the image size in flash. */
        . = ALIGN(4);
        BYTE(0x45);
        BYTE(0x4e);
        BYTE(0x44);
        BYTE(0xea);
	/* NOTHING MAY GO AFTER THIS! */
    } > IRAM

    /* The linker won't notice if the .data section is too big to fit,
     * apparently because we're sending it into IRAM, not FLASH. The following
     * symbol isn't used by the code, but running "objdump -t *.elf | grep hey"
     * will let us check how much flash space we're actually using. The
     * explicit ASSERT afterwards will cause the linker to abort if we use too
     * much. */
    __hey_flash_used = LOADADDR(.data) + SIZEOF(.data) - FW_OFF(SECTION);
    ASSERT(FW_SIZE(SECTION) >
                 (LOADADDR(.data) + SIZEOF(.data) - FW_OFF(SECTION)),
                 "No room left in the flash")
#if !(defined(SECTION_IS_RO) && defined(CONFIG_FLASH))
    /DISCARD/ : {
              *(.google)
              }
#endif

    /DISCARD/ : { *(.ARM.*) }
}
