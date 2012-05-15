/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "config.h"

#define CONFIG_FW_SECT_OFF(section) CONFIG_FW_##section##_OFF
#define CONFIG_FW_BASE(section) (CONFIG_FLASH_BASE + CONFIG_FW_SECT_OFF(section))
#define CONFIG_FMAP_BASE(section) CONFIG_FW_BASE(section) + CONFIG_FMAP_OFF

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(reset)
MEMORY
{
    FLASH (rx) : ORIGIN = CONFIG_FW_BASE(SECTION), LENGTH = CONFIG_FW_IMAGE_SIZE
    IRAM (rw)  : ORIGIN = CONFIG_RAM_BASE, LENGTH = CONFIG_RAM_SIZE
}
SECTIONS
{
    .text : {
        OUTDIR/core/CORE/init.o (.text.vecttable)
        . = ALIGN(4);
        *(.rodata.ver)
        . = ALIGN(4);
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

        . = ALIGN(4);
        *(.rodata*)
        . = ALIGN(4);
#ifdef COMPILE_FOR_RAM
    } > IRAM
    __ro_end = . ;
    .data : {
#else
    } > FLASH
    __ro_end = . ;
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
    } > IRAM
#if defined(SECTION_IS_RO) && defined(CONFIG_VBOOT)
    .google CONFIG_FMAP_BASE(SECTION) : AT(CONFIG_FMAP_BASE(SECTION)) {
        *(.google)
    } > FLASH
#else
    /DISCARD/ : {
              *(.google)
              }
#endif
    .bss : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss)
        . = ALIGN(4);
        __bss_end = .;

	/* Shared memory buffer must be at the end of preallocated RAM, so it
	 * can expand to use all the remaining RAM. */
	__shared_mem_buf = .;
    } > IRAM

    /DISCARD/ : { *(.ARM.*) }
}
