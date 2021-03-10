/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MCHP MEC parts with 416KB SRAM SoC little FW
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
    VECTOR(r )  : ORIGIN =  0x0C0000, LENGTH = 0x18
    SRAM (xrw)  : ORIGIN =  0x0C0018, LENGTH = 0xFE8
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
 * MEC172xN has 416KB total SRAM: 352KB CODE 64KB DATA
 * CODE: 0x0C0000 - 0x117FFF
 * DATA: 0x118000 - 0x127FFF
 * Boot-ROM log is 0x11FF00 - 0x11FFFF
 * MEC172x Top 1KB is not cleared if OTP customer flag enabled.
 * !!! TODO !!! Does presence of PUF feature move customer area?
 * Boot-ROM spec states 3.5KB from top is lost.
 * 0x12_7800 - 0x12_7fff 2KB used by PUF option
 * 0x12_7400 - 0x12_77ff 1KB Customer use. Not cleared by Boot-ROM
 * 0x12_7200 - 0x12_73ff 512 byte Boot-ROM log
 * CrOS EC puts panic data at Top of RAM.
 * We must set Top of RAM to be customer region far enough to
 * hold panic data.
 * Set Top of SRAM to 0x12_7800.
 * This requires size of SRAM = 0x127800 - 0x118000 = 0xF800 (62 KB)
 */
PROVIDE( lfw_stack_top = 0x127800 );

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

    __image_size = LOADADDR(.text) + SIZEOF(.text) - ORIGIN(VECTOR);
}
