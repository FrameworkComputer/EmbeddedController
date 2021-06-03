/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

. = ALIGN(4);
__lfw_text_start = .;
*(.lfw.*)
. = ALIGN(4);
__lfw_text_end = .;

#ifdef CONFIG_PLATFORM_EC_EXTERNAL_STORAGE
. = ALIGN(4);
__flash_lpfw_start = .;
/* Entering deep idle FW for better power consumption */
KEEP(*(.lowpower_ram))
. = ALIGN(4);
__flash_lpfw_end = .;
__flash_lplfw_start = .;
/* GDMA utilities for better FW download speed */
KEEP(*(.lowpower_ram2))
. = ALIGN(4);
__flash_lplfw_end = .;
#endif /* CONFIG_PLATFORM_EC_EXTERNAL_STORAGE */
