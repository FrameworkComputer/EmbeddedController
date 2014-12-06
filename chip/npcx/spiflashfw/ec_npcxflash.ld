/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NPCX5M5G SoC spi flash update tool
 */

/* Memory Spaces Definitions */
MEMORY
{
    CODERAM (rx) : ORIGIN = 0x200C0000, LENGTH = 16K
    RAM    (xrw) : ORIGIN = 0x200C4000, LENGTH = 16K
}

/*
 * The entry point is informative, for debuggers and simulators,
 * since the Cortex-M vector points to it anyway.
 */
ENTRY(sspi_flash_upload)


/* Sections Definitions */

SECTIONS
{
    .startup_text :
    {
        . = ALIGN(4);
        *(.startup_text )	/* Startup code */
        . = ALIGN(4);
    } >CODERAM

    /*
     * The program code is stored in the .text section,
     * which goes to CODERAM.
     */
    .text :
    {
        . = ALIGN(4);
        *(.text .text.*)			/* all remaining code */
        *(.rodata .rodata.*)		/* read-only data (constants) */
    } >CODERAM

    . = ALIGN(4);
    _etext = .;

    /*
     * This address is used by the startup code to
     * initialise the .data section.
     */
    _sidata = _etext;

    /*
     * Used for validation only, do not allocate anything here!
     *
     * This is just to check that there is enough RAM left for the Main
     * stack. It should generate an error if it's full.
     */
    .up_flag :
    {
        . = ALIGN(4);
        *(.up_flag )	/* Startup code */
        . = ALIGN(4);
    } >RAM

    /*
     * The initialised data section.
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

    } >RAM

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
