/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if defined(CONFIG_CPU_CORTEX_M) || defined(CONFIG_CPU_CORTEX_R) || \
	defined(CONFIG_CPU_CORTEX_A)
__image_size = _flash_used;
#elif defined(CONFIG_RISCV)
/* Firmware binary size is text + rodata + data in rom. */
__image_size = _image_rom_size + (__data_ram_end - __data_ram_start);
#else
/*
 * Intentionally set to 0. Some components, such as EFS2, need this value.
 * Having it be 0 will make it easier to find and add new cores.
 */
__image_size = 0;
#endif
