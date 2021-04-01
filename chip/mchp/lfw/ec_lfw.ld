/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MCHP MEC parts with 256KB SRAM SoC little FW
 *
 */

/*
 * Memory Spaces Definitions
 * LFW occupies first 4KB of CODE SRAM.
 * First 24 bytes contain a minimal Cortex-M4
 * vector table.
 */
MEMORY
{
    VECTOR(r )  : ORIGIN =  0x0E0000, LENGTH = 0x18
    SRAM (xrw)  : ORIGIN =  0x0E0018, LENGTH = 0xFE8
}

/*
 * ld does not allow mathematical expressions in ORIGIN/LENGTH, so check the
 * values here.
 */
ASSERT(ORIGIN(VECTOR) + LENGTH(VECTOR) == ORIGIN(SRAM), "Invalid SRAM origin.")
ASSERT(LENGTH(VECTOR) + LENGTH(SRAM) == 0x1000, "Invalid VECTOR+SRAM length.")

/*
 * The entry point is informative, for debuggers and simulators,
 * since the Cortex-M vector points to it anyway.
 */
ENTRY(lfw_main)

/*
 * MEC 256KB SRAM 0xE0000 - 0x11FFFF
 * Data Top 32KB at 0x118000 - 0x11FFFF
 * Boot-ROM log is 0x11FF00 - 0x11FFFF
 * Set top of LFW stack 1KB below top of SRAM
 * because EC panic and jump data live at
 * top of SRAM.
 * !!!WARNING!!!
 * POR or any chip reset will cause MEC BootROM
 * to run. BootROM will clear all CODE & DATA SRAM.
 * Panic data will be lost.
 *
 */
PROVIDE( lfw_stack_top = 0x11F000 );

/* Sections Definitions */

SECTIONS
{

    /*
     * The vector table goes first
     */
    .intvector :
    {
        . = ALIGN(4);
        KEEP(*(.intvector))
    } > VECTOR

    /*
     * The program code is stored in the .text section,
     * which goes to FLASH.
     */

    .text :
    {
		. = ALIGN(4);
		__image_data_offset = .;
		KEEP(*(.rodata.ver))
        *(.text .text.*)                        /* all remaining code */
        *(.rodata .rodata.*)            /* read-only data (constants) */
    } >SRAM

    . = ALIGN(4);

    /* Padding */

    .fill : {
        FILL(0xFF);
        . = ORIGIN(SRAM) + LENGTH(SRAM) - 1;
        BYTE(0xFF);   /* emit at least a byte to make linker happy */
    }

    __image_size = LOADADDR(.text) + SIZEOF(.text) - ORIGIN(VECTOR);
}
