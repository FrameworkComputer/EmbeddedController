/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "config.h"

#define CONFIG_FW_SECT_OFF(section) CONFIG_FW_##section##_OFF
#define CONFIG_FW_BASE(section) (CONFIG_FLASH_BASE + CONFIG_FW_SECT_OFF(section))

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
        *(.rodata.cmds)
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
        __data_end = .;
    } > IRAM
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
