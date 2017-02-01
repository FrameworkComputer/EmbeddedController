/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MEC1322 SoC little FW
 *
 */

/* Memory Spaces Definitions */
MEMORY
{
    VECTOR(r )  : ORIGIN =  0x100000, LENGTH = 24
    SRAM (xrw)  : ORIGIN =  0x100018, LENGTH = 0xC00 - LENGTH(VECTOR)
}

/*
 * The entry point is informative, for debuggers and simulators,
 * since the Cortex-M vector points to it anyway.
 */
ENTRY(lfw_main)

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
}
