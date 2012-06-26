/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "config.h"

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
MEMORY
{
    FLASH (rx) : ORIGIN = CONFIG_FLASH_BASE, LENGTH = CONFIG_FLASH_SIZE
}
SECTIONS
{
    . = ALIGN(CONFIG_FLASH_BANK_SIZE);
    .image.RO : AT(CONFIG_FLASH_BASE + CONFIG_SECTION_RO_OFF) {
        *(.image.RO)
    } > FLASH =0xff
    . = ALIGN(CONFIG_FLASH_BANK_SIZE);
    .image.A : AT(CONFIG_FLASH_BASE + CONFIG_SECTION_A_OFF) {
        *(.image.A)
        BYTE(0xEA)                      /* Mark end explicitly */
    } > FLASH =0xff
#ifdef CONFIG_RW_B
    . = ALIGN(CONFIG_FLASH_BANK_SIZE);
    .image.B : AT(CONFIG_FLASH_BASE + CONFIG_SECTION_B_OFF) {
        *(.image.B)
        BYTE(0xEB)                      /* Mark end explicitly */
    } > FLASH =0xff
#endif
    .padding : AT(CONFIG_FLASH_BASE + CONFIG_FLASH_SIZE - 1) {
        BYTE(0xff);
    } > FLASH =0xff
}
