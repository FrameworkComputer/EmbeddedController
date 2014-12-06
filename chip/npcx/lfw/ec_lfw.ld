/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NPCX5M5G SoC little FW for booting
 */

/* Memory Spaces Definitions */
MEMORY
{
    FLASH (rx) : ORIGIN = 0x647FF000, LENGTH = 4K - 256
    POINTER(r) : ORIGIN = 0x647FFF00, LENGTH = 256
    CODERAM(rx): ORIGIN = 0x100A7C00, LENGTH = 1K - 256
    RAM (xrw)  : ORIGIN = 0x100A7F00, LENGTH = 256
}

/*
 * The entry point is informative, for debuggers and simulators,
 * since the Cortex-M vector points to it anyway.
 */
ENTRY(entry_lfw)

/* Sections Definitions */

SECTIONS
{

    /*
     * The beginning of the startup code
     */
    .startup_text :
    {
        . = ALIGN(4);
        *(.startup_text )	/* Startup code */
        . = ALIGN(4);
    } >FLASH


    /*
     * The program code is stored in the .text section,
     * which goes to FLASH.
     */
    .text :
    {
        . = ALIGN(4);
        *(.text .text.*)			/* all remaining code */
        *(.rodata .rodata.*)		/* read-only data (constants) */
    } >FLASH

    . = ALIGN(4);

    __flash_fw_start = .;
    .instrucion_ram : AT(__flash_fw_start)
    {
        . = ALIGN(4);
        __iram_fw_start = .;
        *(.instrucion_ram .instrucion_ram.*)    /* CODERAM in 0x200C0000 */
        __iram_fw_end = .;
    } > CODERAM

    /*
     * The POINTER section used for booter
     */
    .booter_pointer :
    {
        . = ALIGN(4);
        KEEP(*(.booter_pointer))	/* Booter pointer in 0xFFFF00 */
    } > POINTER

    . = ALIGN(4);
    _etext = .;

    /*
     * This address is used by the startup code to
     * initialise the .data section.
     */
    _sidata = _etext;

    /*
     * The initialised data section.
     * The program executes knowing that the data is in the RAM
     * but the loader puts the initial values in the FLASH (inidata).
     * It is one task of the startup to copy the initial values from
     * FLASH to RAM.
     */
    .data  : AT ( _sidata )
    {
        . = ALIGN(4);

        /* This is used by the startup code to initialise the .data section */
        __data_start__ = . ;
        *(.data_begin .data_begin.*)

        *(.data .data.*)

        *(.data_end .data_end.*)
        . = ALIGN(4);

        /* This is used by the startup code to initialise the .data section */
        __data_end__ = . ;

    } > RAM


    /*
     * The uninitialised data section. NOLOAD is used to avoid
     * the "section `.bss' type changed to PROGBITS" warning
     */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start__ = .;	/* standard newlib definition */
        *(.bss_begin .bss_begin.*)

        *(.bss .bss.*)
        *(COMMON)

        *(.bss_end .bss_end.*)
        . = ALIGN(4);
        __bss_end__ = .;        /* standard newlib definition */
    } >RAM
}