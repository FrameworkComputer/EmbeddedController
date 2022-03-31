/* Copyright 2022 The Chromium OS Authors. All rights reserved.
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
__flash_lplfw_start = .;
/* QSPI load and jump to EC image */
KEEP(*(.code_in_sram2))
. = ALIGN(4);
__flash_lplfw_end = .;
#endif /* CONFIG_PLATFORM_EC_EXTERNAL_STORAGE */
