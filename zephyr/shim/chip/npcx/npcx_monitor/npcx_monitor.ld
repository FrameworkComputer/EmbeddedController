/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NPCX SoC spi flash update tool
 */

/* Memory Spaces Definitions */
MEMORY
{
    CODERAM (rx) : ORIGIN = 0x200C3020, LENGTH = 0xFE0
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

}
