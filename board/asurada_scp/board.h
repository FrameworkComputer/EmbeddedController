/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Asurada SCP configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_FLASH_SIZE 0x58000
#define CONFIG_LTO
#define CONFIG_UART_CONSOLE 0

/*
 * RW only, no flash
 * +-------------------- 0x0
 * | ROM vectortable, .text, .rodata, .data LMA
 * +-------------------- 0x58000
 * | RAM .bss, .data
 * +-------------------- 0x7BDB0
 * | IPI shared buffer with AP (288 + 8) * 2
 * +-------------------- 0x7C000
 * | 8KB I-CACHE
 * +-------------------- 0x7E000
 * | 8KB D-CACHE
 * +-------------------- 0x80000
 */
#define ICACHE_BASE 0x7C000
#define CONFIG_ROM_BASE 0x0
#define CONFIG_RAM_BASE 0x58000
#define CONFIG_ROM_SIZE (CONFIG_RAM_BASE - CONFIG_ROM_BASE)
#define CONFIG_RAM_SIZE (CONFIG_IPC_SHARED_OBJ_ADDR - CONFIG_RAM_BASE)
#define CONFIG_CODE_RAM_SIZE CONFIG_RAM_BASE
#define CONFIG_DATA_RAM_SIZE (ICACHE_BASE - CONFIG_RAM_BASE)
#define CONFIG_RO_MEM_OFF 0

/* IPI configs */
#define CONFIG_IPC_SHARED_OBJ_BUF_SIZE 288
#define CONFIG_IPC_SHARED_OBJ_ADDR                                             \
	(ICACHE_BASE -                                                         \
	 (CONFIG_IPC_SHARED_OBJ_BUF_SIZE + 2 * 4 /* int32_t */) * 2)

#ifndef __ASSEMBLER__
#include "gpio_signal.h"
#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
