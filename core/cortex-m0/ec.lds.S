/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "config.h"
#include "rsa.h"

#define FW_OFF_(section) CONFIG_FW_##section##_OFF
#define FW_OFF(section) (CONFIG_FLASH_BASE + FW_OFF_(section))

#define FW_SIZE_(section) CONFIG_FW_##section##_SIZE
#define FW_SIZE(section) FW_SIZE_(section)


OUTPUT_FORMAT(BFD_FORMAT, BFD_FORMAT, BFD_FORMAT)
OUTPUT_ARCH(BFD_ARCH)
ENTRY(reset)
MEMORY
{
    FLASH (rx) : ORIGIN = FW_OFF(SECTION), LENGTH = FW_SIZE(SECTION)
    IRAM (rw)  : ORIGIN = CONFIG_RAM_BASE, LENGTH = CONFIG_RAM_SIZE
#ifdef RSA_PUBLIC_KEY_SIZE
    PSTATE(r)  : ORIGIN = FW_OFF(SECTION) + FW_SIZE(SECTION), \
                 LENGTH = CONFIG_FW_PSTATE_SIZE
#endif
#ifdef CONFIG_USB
    USB_RAM (rw) : \
        ORIGIN = CONFIG_USB_RAM_BASE, \
        LENGTH = CONFIG_USB_RAM_SIZE * CONFIG_USB_RAM_ACCESS_SIZE / 2
#endif
}
SECTIONS
{
    .text : {
        OUTDIR/core/CORE/init.o (.text.vecttable)
        . = ALIGN(4);
        __version_struct_offset = .;
        KEEP(*(.rodata.ver))
#ifdef SHIFT_CODE_FOR_TEST
        . = ALIGN(256);
#else
        . = ALIGN(4);
#endif
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
        KEEP(*(.rodata.irqprio))
        __irqprio_end = .;

        . = ALIGN(4);
        __cmds = .;
        KEEP(*(SORT(.rodata.cmds*)))
        __cmds_end = .;

        . = ALIGN(4);
        __hcmds = .;
        KEEP(*(.rodata.hcmds))
        __hcmds_end = .;

	. = ALIGN(4);
	__mkbp_evt_srcs = .;
	KEEP(*(.rodata.evtsrcs))
	__mkbp_evt_srcs_end = .;

        . = ALIGN(4);
        __hooks_init = .;
        KEEP(*(.rodata.HOOK_INIT))
        __hooks_init_end = .;

        __hooks_pre_freq_change = .;
        KEEP(*(.rodata.HOOK_PRE_FREQ_CHANGE))
        __hooks_pre_freq_change_end = .;

        __hooks_freq_change = .;
        KEEP(*(.rodata.HOOK_FREQ_CHANGE))
        __hooks_freq_change_end = .;

        __hooks_sysjump = .;
        KEEP(*(.rodata.HOOK_SYSJUMP))
        __hooks_sysjump_end = .;

        __hooks_chipset_pre_init = .;
        KEEP(*(.rodata.HOOK_CHIPSET_PRE_INIT))
        __hooks_chipset_pre_init_end = .;

        __hooks_chipset_startup = .;
        KEEP(*(.rodata.HOOK_CHIPSET_STARTUP))
        __hooks_chipset_startup_end = .;

        __hooks_chipset_resume = .;
        KEEP(*(.rodata.HOOK_CHIPSET_RESUME))
        __hooks_chipset_resume_end = .;

        __hooks_chipset_suspend = .;
        KEEP(*(.rodata.HOOK_CHIPSET_SUSPEND))
        __hooks_chipset_suspend_end = .;

        __hooks_chipset_shutdown = .;
        KEEP(*(.rodata.HOOK_CHIPSET_SHUTDOWN))
        __hooks_chipset_shutdown_end = .;

        __hooks_ac_change = .;
        KEEP(*(.rodata.HOOK_AC_CHANGE))
        __hooks_ac_change_end = .;

        __hooks_lid_change = .;
        KEEP(*(.rodata.HOOK_LID_CHANGE))
        __hooks_lid_change_end = .;

        __hooks_pwrbtn_change = .;
        KEEP(*(.rodata.HOOK_POWER_BUTTON_CHANGE))
        __hooks_pwrbtn_change_end = .;

        __hooks_charge_state_change = .;
        KEEP(*(.rodata.HOOK_CHARGE_STATE_CHANGE))
        __hooks_charge_state_change_end = .;

        __hooks_tick = .;
        KEEP(*(.rodata.HOOK_TICK))
        __hooks_tick_end = .;

        __hooks_second = .;
        KEEP(*(.rodata.HOOK_SECOND))
        __hooks_second_end = .;

        __deferred_funcs = .;
        KEEP(*(.rodata.deferred))
        __deferred_funcs_end = .;

        __usb_desc = .;
        KEEP(*(.rodata.usb_desc_conf))
        KEEP(*(SORT(.rodata.usb_desc*)))
        . = ALIGN(2); /* USB transfers enjoy 16-bit aligned size */
        __usb_desc_end = .;
        . = ALIGN(4);
        KEEP(*(.rodata.usb_ep))

        . = ALIGN(4);
        *(.rodata*)

#if defined(SECTION_IS_RO) && defined(CONFIG_FLASH)
        . = ALIGN(64);
        KEEP(*(.google))
#endif
        . = ALIGN(4);
#ifdef COMPILE_FOR_RAM
    } > IRAM
#else
    } > FLASH
#endif
    __ro_end = . ;

    __deferred_funcs_count =
		(__deferred_funcs_end - __deferred_funcs) / 4;
    ASSERT(__deferred_funcs_count <= DEFERRABLE_MAX_COUNT,
           "Increase DEFERRABLE_MAX_COUNT")

    .bss : {
	/*
	 * Vector table must be at the beginning of bss section. The vector
	 * table section contains a RAM copy of the vector table used on
	 * STM chips for relocating the vector table.
	 */
	. = ALIGN(8);
	__bss_start = .;
	*(.bss.vector_table)
	/* Stacks must be 64-bit aligned */
        . = ALIGN(8);
        *(.bss.system_stack)
	/* Rest of .bss takes care of its own alignment */
        *(.bss)
        . = ALIGN(4);
        __bss_end = .;
    } > IRAM
#ifdef COMPILE_FOR_RAM
    .data : {
#else
    .data : AT(ADDR(.rodata) + SIZEOF(.rodata)) {
#endif
        . = ALIGN(4);
        __data_start = .;
        *(.data.tasks)
        *(.data)
        . = ALIGN(4);
        *(.iram.text)
        . = ALIGN(4);
        __data_end = .;

	/* Shared memory buffer must be at the end of preallocated RAM, so it
	 * can expand to use all the remaining RAM. */
	__shared_mem_buf = .;

	/* Tag at end of firmware image so that we can find the image size.
	 * This may be overwritten by the shared memory buffer; that's ok
	 * because we only use it to find the image size in flash. */
        . = ALIGN(4);
        BYTE(0x45);
        BYTE(0x4e);
        BYTE(0x44);
        BYTE(0xea);
	/* NOTHING MAY GO AFTER THIS! */
    } > IRAM

#ifdef RSA_PUBLIC_KEY_SIZE
#ifdef SECTION_IS_RO
    .flash_suffix : {
        FILL(0xff);
        /*
	 * Put the public key coefficients at the end of the partition
	 * after the pstate bits.
	 */
        . = ORIGIN(PSTATE) + LENGTH(PSTATE) - RSA_PUBLIC_KEY_SIZE;
        *(.rsa_pubkey)
    } > PSTATE
#else /* RW section: we don't need the RSA public key, put it anywhere */
    .flash_suffix  : AT(LOADADDR(.data) + SIZEOF(.data)) {
        *(.rsa_pubkey)
    } > FLASH
#endif
#endif

    /* The linker won't notice if the .data section is too big to fit,
     * apparently because we're sending it into IRAM, not FLASH. The following
     * symbol isn't used by the code, but running "objdump -t *.elf | grep hey"
     * will let us check how much flash space we're actually using. The
     * explicit ASSERT afterwards will cause the linker to abort if we use too
     * much. */
    __hey_flash_used = LOADADDR(.data) + SIZEOF(.data) - FW_OFF(SECTION);
    ASSERT(FW_SIZE(SECTION) >
                 (LOADADDR(.data) + SIZEOF(.data) - FW_OFF(SECTION)),
                 "No room left in the flash")

#ifdef CONFIG_USB
    .usb_ram (NOLOAD) : {
        __usb_ram_start = .;
        . = ALIGN(8);
        *(.usb_ram.btable)
        *(.usb_ram.data)
    } > USB_RAM
#endif
#if !(defined(SECTION_IS_RO) && defined(CONFIG_FLASH))
    /DISCARD/ : {
              *(.google)
              }
#endif

    /DISCARD/ : { *(.ARM.*) }
}
