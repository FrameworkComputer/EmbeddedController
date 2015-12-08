/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"

OUTPUT_FORMAT(BFD_FORMAT, BFD_FORMAT, BFD_FORMAT)
OUTPUT_ARCH(BFD_ARCH)
MEMORY
{
    FLASH (rx) : ORIGIN = CONFIG_PROGRAM_MEMORY_BASE, LENGTH = CONFIG_FLASH_SIZE
}
SECTIONS
{
    . = ALIGN(CONFIG_FLASH_BANK_SIZE);
#if defined(NPCX_RO_HEADER)
/* Replace *_MEM_OFF with *_STORAGE_OFF to indicate flat file contains header
 * or some struture which doesn't belong to FW */
    .image.RO : AT(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_EC_PROTECTED_STORAGE_OFF\
     ) {
#else
    .image.RO : AT(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RO_MEM_OFF) {
#endif
        *(.image.RO)
    } > FLASH =0xff
    . = ALIGN(CONFIG_FLASH_BANK_SIZE);
#ifdef CONFIG_SHAREDLIB
    .image.libsharedobjs : AT(CONFIG_PROGRAM_MEMORY_BASE + \
                              CONFIG_SHAREDLIB_MEM_OFF) {
        *(.image.libsharedobjs)
    } > FLASH =0xff
    . = ALIGN(CONFIG_FLASH_BANK_SIZE);
#endif

#if defined(NPCX_RO_HEADER)
    /* npcx uses *STORAGE_OFF to plan the layout of flash image */
    .image.RW : AT(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_EC_WRITABLE_STORAGE_OFF \
     + CONFIG_RW_STORAGE_OFF) {
#elif (CONFIG_RO_MEM_OFF == CONFIG_RW_MEM_OFF)
/* This is applicable to ECs in which RO and RW execution is
mapped to the same location but we still have to generate an ec.bin with RO
and RW images at different Flash offset */
    .image.RW : AT(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RO_MEM_OFF + \
                   CONFIG_RO_SIZE) {
#else
    .image.RW : AT(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_MEM_OFF) {
#endif
        *(.image.RW)
    } > FLASH =0xff
#ifdef CONFIG_RW_B_MEM_OFF
    .image.RW_B : AT(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_B_MEM_OFF) {
        *(.image.RW_B)
    } > FLASH =0xff
#endif
    .padding : AT(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_FLASH_SIZE - 1) {
        BYTE(0xff);
    } > FLASH =0xff
}
