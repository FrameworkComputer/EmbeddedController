/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"

OUTPUT_FORMAT(BFD_FORMAT, BFD_FORMAT, BFD_FORMAT)
OUTPUT_ARCH(BFD_ARCH)
MEMORY
{
    FLASH (rx) : ORIGIN = CONFIG_FLASH_BASE, LENGTH = CONFIG_FLASH_SIZE
}
SECTIONS
{
    . = ALIGN(CONFIG_FLASH_BANK_SIZE);
    .image.RO : AT(CONFIG_FLASH_BASE + CONFIG_FW_RO_OFF) {
        *(.image.RO)
    } > FLASH =0xff
    . = ALIGN(CONFIG_FLASH_BANK_SIZE);
    .image.RW : AT(CONFIG_FLASH_BASE + CONFIG_FW_RW_OFF) {
        *(.image.RW)
    } > FLASH =0xff
    .padding : AT(CONFIG_FLASH_BASE + CONFIG_FLASH_SIZE - 1) {
        BYTE(0xff);
    } > FLASH =0xff
}
