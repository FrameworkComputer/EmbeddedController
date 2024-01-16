/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MCHP SOC spi flash update tool
 */

/* Memory Spaces Definitions */
MEMORY
{
    CODE_SRAM (rx) : ORIGIN = 0x000C0000, LENGTH = 40K
    BUFFER_4K (rw) : ORIGIN = 0x000CA000, LENGTH = 4K
    BUFFER_256K (rw) : ORIGIN = 0x000CB000, LENGTH = 256K
    DATA_SRAM (rwx) : ORIGIN = 0x00126000, LENGTH = 0x00001800
}
/*
 * The entry point is informative, for debuggers and simulators,
 * since the Cortex-M vector points to it anyway.
 */
ENTRY(Reset_Handler)


/* Sections Definitions */

SECTIONS
{
	.text :
	{
		KEEP(*(.vectors))
		__isr_vector_end = .;
		__isr_vector_size = __isr_vector_end - __Vectors;

		*(.text*)
		KEEP(*(.init))
		KEEP(*(.fini))
		*(.rodata*)
		KEEP(*(.eh_frame*))
	} > CODE_SRAM

	/* Location counter can end up 2byte aligned with narrow Thumb code but
	   __etext is assumed by startup code to be the LMA of a section in RAM
	   which must be 4byte aligned */
	__etext = ALIGN (4);

	.data : AT (__etext)
	{
		__data_start__ = .;
		*(vtable)
		*(.data*)

		. = ALIGN(4);
		__data_end__ = .;

	} > DATA_SRAM

	.bss (NOLOAD):
	{
		. = ALIGN(4);
		__bss_start__ = .;
		*(.bss*)
		. = ALIGN(4);
		__bss_end__ = .;
	} > DATA_SRAM

	.buffer_4K (NOLOAD):
	{
		*(.buffer_4K)
	} > BUFFER_4K

	.buffer_256K (NOLOAD):
	{
		*(.buffer_256K)
	} > BUFFER_256K

	/* .stack_dummy section doesn't contains any symbols. It is only
	 * used for linker to calculate size of stack sections, and assign
	 * values to stack symbols later */
	.stack_dummy (COPY):
	{
		*(.stack*)
	} > DATA_SRAM

	/* Set stack top to end of RAM, and stack limit move down by
	 * size of stack_dummy section */
	__StackTop = ORIGIN(DATA_SRAM) + LENGTH(DATA_SRAM);
	__StackLimit = __StackTop - SIZEOF(.stack_dummy);
	PROVIDE(__stack = __StackTop);

	/* Check if data + heap + stack exceeds RAM limit */
	ASSERT(__StackLimit >= __bss_end__, "region RAM overflowed with stack")
}
