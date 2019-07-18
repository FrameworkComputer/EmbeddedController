/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <config_chip.h>

ENTRY(ish_aon_main);

#define SRAM_START	CONFIG_AON_RAM_BASE
#define SRAM_RW_LEN	(CONFIG_AON_RAM_SIZE - CONFIG_AON_PERSISTENT_SIZE)

/* reserved stack size */
#define STACK_SIZE  (256)

/**
 * resered 8 bytes for GDB showing correct stack
 * information during source code level debuging
 */
#define RESERVED_GDB_SIZE  (8)

#define RAM_LEN (SRAM_RW_LEN - STACK_SIZE - RESERVED_GDB_SIZE)

/**
 *  AON memory layout
 * +---------+------------+-----------------+-----------------+
 * | RAM_LEN | STACK_SIZE | 8 Bytes for GDB | ROM (384 Bytes) |
 * +---------+------------+-----------------+-----------------+
 *
 * The first 256 bytes of the AON ROM is reserved for ECOS use.
 * The remaining 128 bytes of the AON ROM may be used by the shim
 * loader.
 */

MEMORY
{
    /* leave STACK_SIZE bytes in the end of memory for stack */
    RAM : ORIGIN = SRAM_START, LENGTH = RAM_LEN
}

SECTIONS
{
    /* AON parts visible to FW are linked to the beginning of the AON area */
    .data.aon_share : AT(SRAM_START)
    {
	KEEP(*(.data.aon_share))
    } > RAM

    .data :
    {
	*(.data)
	    *(.data*)
    } > RAM

    .text :
    {
	*(.text)
	    *(.text*)
    } > RAM

    .bss :
    {
	*(.bss)
	    *(.bss*)
    } > RAM

    .stack_tag :
    {
	KEEP(*(.stack_tag))
    } > RAM

}
