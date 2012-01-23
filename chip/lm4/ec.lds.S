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
        OUTDIR/chip/CHIP/init.o (.text)
        *(.text*)
#ifdef COMPILE_FOR_RAM
    } > IRAM
#else
    } > FLASH
#endif
    . = ALIGN(4);
    .rodata : {
        __irqprio = .;
        *(.rodata.irqprio)
        __irqprio_end = .;
        __cmds = .;
        *(.rodata.cmds)
        __cmds_end = .;
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
    } > IRAM
    /DISCARD/ : { *(.ARM.*) }
}
