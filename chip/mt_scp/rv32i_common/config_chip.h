/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

#include "core/riscv-rv32i/config_core.h"

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 500
#define HOOK_TICK_INTERVAL (HOOK_TICK_INTERVAL_MS * MSEC)

/* RW only, no flash */
#undef CONFIG_FW_INCLUDE_RO
#define CONFIG_RO_MEM_OFF 0
#define CONFIG_RO_SIZE 0
#define CONFIG_RW_MEM_OFF 0
#if defined(BOARD_CHERRY_SCP_CORE1) || defined(BOARD_GERALT_SCP_CORE1)
#define CONFIG_RW_SIZE SCP_CORE1_RAM_SIZE
#else
#define CONFIG_RW_SIZE 0x40000 /* 256KB */
#endif
#define CONFIG_EC_WRITABLE_STORAGE_OFF 0
#define CONFIG_EC_PROTECTED_STORAGE_OFF 0
#define CONFIG_RO_STORAGE_OFF 0
#define CONFIG_RW_STORAGE_OFF 0
#define CONFIG_PROGRAM_MEMORY_BASE 0
#if defined(BOARD_CHERRY_SCP_CORE1) || defined(BOARD_GERALT_SCP_CORE1)
#define CONFIG_PROGRAM_MEMORY_BASE_LOAD (0x10500000 + SCP_CORE1_RAM_START)
#else
#define CONFIG_PROGRAM_MEMORY_BASE_LOAD 0x10500000
#endif
#define CONFIG_MAPPED_STORAGE_BASE 0

/* Unsupported features/commands */
#undef CONFIG_CMD_FLASHINFO
#undef CONFIG_CMD_POWER_AP
#undef CONFIG_FLASH_CROS
#undef CONFIG_FLASH_PHYSICAL
#undef CONFIG_FMAP
#undef CONFIG_HIBERNATE
#undef CONFIG_LID_SWITCH

/* Task stack size */
#define CONFIG_STACK_SIZE 1024
#define IDLE_TASK_STACK_SIZE 640
#define SMALLER_TASK_STACK_SIZE 384
#define TASK_STACK_SIZE 488
#define LARGER_TASK_STACK_SIZE 640
#define VENTI_TASK_STACK_SIZE 768
#define ULTRA_TASK_STACK_SIZE 1056
#define TRENTA_TASK_STACK_SIZE 1184

/* TODO: need to confirm, placeholder */
#define GPIO_PIN(num) ((num) / 32), ((num) % 32)
#define GPIO_PIN_MASK(p, m) .port = (p), .mask = (m)
#undef CONFIG_TASK_PROFILING
/* TODO: not yet supported */
#undef CONFIG_MPU
/* TODO: core/riscv-rv32i pollution */
#define __ram_code

#endif /* __CROS_EC_CONFIG_CHIP_H */
