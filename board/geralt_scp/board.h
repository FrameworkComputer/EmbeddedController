/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Cherry SCP configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "baseboard.h"

#define SCP_CORE1_RAM_START 0xe0000
#define SCP_CORE1_RAM_SIZE 0x1F000 /* 124K */
#define SCP_CORE1_RAM_PADDING 0xc00 /* for 4K-alignment */

#ifdef BOARD_GERALT_SCP_CORE1

/*
 * RW only, no flash
 * +-------------------- 0xe0000 + 0
 * | ROM vectortable, .text, .rodata, .data LMA
 * +-------------------- 0xe0000 + 0x14000 = 0xf4000
 * | RAM .bss, .data
 * +-------------------- 0xe0000 + 0x1ec00 = 0xfec00
 * | Reserved (padding for 1k-alignment)
 * +-------------------- 0xe0000 + 0x1edb0 = 0xfedb0
 * | IPI shared buffer with AP (288 + 8) * 2
 * +-------------------- 0xe0000 + 0x1f000 = 0xff000
 *
 * [Memory remap]
 * SCP core 1 has registers to remap core view addresses by SCP bus. This is
 * useful to boot SCP core 1 because SCP core 0/1 both default read instructions
 * on address 0 when boot up.
 *
 * The core address 0x0~0x10000 are translated to 0xaf000~0xbf000.
 */
#define CONFIG_ROM_BASE 0x0
#define CONFIG_RAM_BASE 0x14000
#define CONFIG_ROM_SIZE (CONFIG_RAM_BASE - CONFIG_ROM_BASE)
#define CONFIG_RAM_SIZE \
	((CONFIG_IPC_SHARED_OBJ_ADDR & (~(0x400 - 1))) - CONFIG_RAM_BASE)

/* SCP_FW_END is used to calc the base of IPI buffer for AP.
 * Provide AP view physical address which include the offset.
 */
#define SCP_FW_END SCP_CORE1_RAM_SIZE

#else

/*
 * RW only, no flash
 * +-------------------- 0x0
 * | ROM vectortable, .text, .rodata, .data LMA
 * +-------------------- 0x68000
 * | RAM .bss, .data
 * +-------------------- 0xe0000 (4k-alignment)
 * | SCP core1 boot code
 * +-------------------- 0xff000 (4k-alignment)
 * | Reserved (padding for 4k-alignment)
 * +-------------------- 0xffc00
 * | Reserved (padding for 1k-alignment)
 * +-------------------- 0xffdb0
 * | IPI shared buffer with AP (288 + 8) * 2
 * +-------------------- 0x100000
 */
#define CONFIG_ROM_BASE 0x0
#define CONFIG_RAM_BASE 0x68000
#define CONFIG_ROM_SIZE (CONFIG_RAM_BASE - CONFIG_ROM_BASE)
#define CONFIG_RAM_SIZE                                                    \
	((CONFIG_IPC_SHARED_OBJ_ADDR & (~(0x400 - 1))) - CONFIG_RAM_BASE - \
	 SCP_CORE1_RAM_SIZE - SCP_CORE1_RAM_PADDING)

#define SCP_FW_END 0x100000

#endif /* BOARD_GERALT_SCP_CORE1 */
#endif /* __CROS_EC_BOARD_H */
