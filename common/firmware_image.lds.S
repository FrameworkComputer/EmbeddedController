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
#if (CONFIG_FW_RO_OFF == CONFIG_FW_RW_OFF)
/* This is applicable to ECs in which RO and RW execution is
mapped to the same location but we still have to generate an ec.bin with RO
and RW images at different Flash offset */
    .image.RW : AT(CONFIG_FLASH_BASE + CONFIG_FW_RO_OFF + CONFIG_FW_RO_SIZE) {
#else
    .image.RW : AT(CONFIG_FLASH_BASE + CONFIG_FW_RW_OFF) {
#endif
        *(.image.RW)
    } > FLASH =0xff
    .padding : AT(CONFIG_FLASH_BASE + CONFIG_FLASH_SIZE - 1) {
        BYTE(0xff);
    } > FLASH =0xff
}
