/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kukui SCP configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_FLASH_SIZE 0x58000 /* Image file size: 256KB */
#undef  CONFIG_LID_SWITCH
#undef  CONFIG_FW_INCLUDE_RO
#define CONFIG_MKBP_EVENT
/* Sent MKBP event via IPI. */
#define CONFIG_MKBP_USE_CUSTOM
#define CONFIG_FPU
#define CONFIG_PRESERVE_LOGS

#define CONFIG_HOSTCMD_ALIGNED

/*
 * RW only, no flash
 * +-------------------- 0x0
 * | ROM vectortable, .text, .rodata, .data LMA
 * +-------------------- 0x10000
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

/* Access DRAM through cached access */
#define CONFIG_DRAM_BASE 0x10000000
/* Shared memory address in AP physical address space. */
#define CONFIG_DRAM_BASE_LOAD 0x50000000
#define CONFIG_DRAM_SIZE 0x01400000 /* 20 MB */

/* IPI configs */
#define CONFIG_IPC_SHARED_OBJ_BUF_SIZE 288
#define CONFIG_IPC_SHARED_OBJ_ADDR                                             \
	(ICACHE_BASE -                                                         \
	 (CONFIG_IPC_SHARED_OBJ_BUF_SIZE + 2 * 4 /* int32_t */) * 2)
#define CONFIG_IPI
#define CONFIG_RPMSG_NAME_SERVICE

#define CONFIG_LTO

/* IPI ID should be in sync across kernel and EC. */
#define IPI_SCP_INIT 0
#define IPI_VDEC_H264 1
#define IPI_VDEC_VP8 2
#define IPI_VDEC_VP9 3
#define IPI_VENC_H264 4
#define IPI_VENC_VP8 5
#define IPI_MDP_INIT 6
#define IPI_MDP_DEINIT 7
#define IPI_MDP_FRAME 8
#define IPI_DIP 9
#define IPI_ISP_CMD 10
#define IPI_ISP_FRAME 11
#define IPI_FD_CMD 12
#define IPI_HOST_COMMAND 13
#define IPI_COUNT 14

#define IPI_NS_SERVICE 0xFF

#define CONFIG_SUPPRESSED_HOST_COMMANDS \
	EC_CMD_CONSOLE_SNAPSHOT, EC_CMD_CONSOLE_READ

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 8192

#undef  CONFIG_UART_CONSOLE
/*
 * CONFIG_UART_CONSOLE
 *   0 - SCP UART0
 *   1 - SCP UART1
 *   2 - share with AP UART0
 */
#define CONFIG_UART_CONSOLE 0

/* We let AP setup the correct pinmux. */
#undef    UART0_PINMUX_11_12
#undef    UART0_PINMUX_110_112

/* Track AP power state */
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE

/* Debugging features */
#define CONFIG_DEBUG_EXCEPTIONS
#define CONFIG_DEBUG_STACK_OVERFLOW
#define CONFIG_CMD_GPIO_EXTENDED

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
