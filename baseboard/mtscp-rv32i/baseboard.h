/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MT SCP RV32i board configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

#define CC_DEFAULT (CC_ALL & ~(CC_MASK(CC_HOSTCMD) | CC_MASK(CC_IPI)))

#define CONFIG_FLASH_SIZE_BYTES CONFIG_RAM_BASE
#define CONFIG_LTO
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_UART_CONSOLE 0

/* IPI configs */
#define CONFIG_IPC_SHARED_OBJ_BUF_SIZE 288
#define CONFIG_IPC_SHARED_OBJ_ADDR \
	(SCP_FW_END -              \
	 (CONFIG_IPC_SHARED_OBJ_BUF_SIZE + 2 * 4 /* int32_t */) * 2)
#define CONFIG_IPI
#define CONFIG_RPMSG_NAME_SERVICE

#define SCP_IPI_INIT 0
#define SCP_IPI_VDEC_H264 1
#define SCP_IPI_VDEC_VP8 2
#define SCP_IPI_VDEC_VP9 3
#define SCP_IPI_VENC_H264 4
#define SCP_IPI_VENC_VP8 5
#define SCP_IPI_MDP_INIT 6
#define SCP_IPI_MDP_DEINIT 7
#define SCP_IPI_MDP_FRAME 8
#define SCP_IPI_DIP 9
#define SCP_IPI_ISP_CMD 10
#define SCP_IPI_ISP_FRAME 11
#define SCP_IPI_FD_CMD 12
#define SCP_IPI_HOST_COMMAND 13
#define SCP_IPI_VDEC_LAT 14
#define SCP_IPI_VDEC_CORE 15
#define SCP_IPI_COUNT 16

#define IPI_COUNT SCP_IPI_COUNT

#define SCP_IPI_NS_SERVICE 0xFF

/* Access DRAM through cached access */
#define CONFIG_DRAM_BASE 0x10000000
/* Shared memory address in AP physical address space. */
#define CONFIG_DRAM_BASE_LOAD 0x50000000
#define CONFIG_DRAM_SIZE 0x01400000 /* 20 MB */

/* Add some space (0x100) before panic for jump data */
#define CONFIG_PANIC_DRAM_SIZE 0x00001000 /* 4K */
#define CONFIG_PANIC_DRAM_BASE (CONFIG_DRAM_BASE + CONFIG_DRAM_SIZE - CONFIG_PANIC_DRAM_SIZE)

/* MPU settings */
#define NR_MPU_ENTRIES 16

#ifndef __ASSEMBLER__
#include "gpio_signal.h"
#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
