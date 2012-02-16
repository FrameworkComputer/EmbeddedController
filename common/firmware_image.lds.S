/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
    .image.RO : AT(CONFIG_FW_RO_OFF) {
        *(.image.RO)
    } > FLASH
    . = ALIGN(CONFIG_FLASH_BANK_SIZE);
    .image.A : AT(CONFIG_FW_A_OFF) {
        *(.image.A)
    } > FLASH
#ifndef CONFIG_NO_RW_B
    . = ALIGN(CONFIG_FLASH_BANK_SIZE);
    .image.B : AT(CONFIG_FW_B_OFF) {
        *(.image.B)
    } > FLASH
#endif
}
