/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"

/*
 * Set the chip family version to 4 digits to keep the flexibility in case
 * we need the minor version for chip variants in a family.
 */
#define NPCX_FAMILY_NPCX5        5000
#define NPCX_FAMILY_NPCX7        7000
#define NPCX_FAMILY_NPCX9        9000

/* Features depend on chip family */
#if defined(CHIP_FAMILY_NPCX5)
#include "config_chip-npcx5.h"
#define NPCX_FAMILY_VERSION      NPCX_FAMILY_NPCX5
#elif defined(CHIP_FAMILY_NPCX7)
#include "config_chip-npcx7.h"
#define NPCX_FAMILY_VERSION      NPCX_FAMILY_NPCX7
#elif defined(CHIP_FAMILY_NPCX9)
#include "config_chip-npcx9.h"
#define NPCX_FAMILY_VERSION      NPCX_FAMILY_NPCX9
#else
#error "Unsupported chip family"
#endif

/* 32k hz internal oscillator frequency (FRCLK) */
#define INT_32K_CLOCK 32768

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 64

/* Use a bigger console output buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 1024

/*
 * Interval between HOOK_TICK notifications
 * Notice instant wake-up from deep-idle cannot exceed 200 ms
 */
#define HOOK_TICK_INTERVAL_MS 200
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* System stack size */
#define CONFIG_STACK_SIZE       1024

/* non-standard task stack sizes */
#define IDLE_TASK_STACK_SIZE		672
#define LARGER_TASK_STACK_SIZE		800
#define VENTI_TASK_STACK_SIZE		928
#define ULTRA_TASK_STACK_SIZE		1056
#define TRENTA_TASK_STACK_SIZE		1184

#define CHARGER_TASK_STACK_SIZE		800
#define HOOKS_TASK_STACK_SIZE		800
#define CONSOLE_TASK_STACK_SIZE		800

/* Default task stack size */
#define TASK_STACK_SIZE			672

/* Address of RAM log used by Booter */
#define ADDR_BOOT_RAMLOG        0x100C7FC0

#include "config_flash_layout.h"

/* Optional features present on this chip */
#define CONFIG_ADC
#define CONFIG_RTC
#define CONFIG_SWITCH
#define CONFIG_MPU

/* Chip needs to do custom pre-init */
#define CONFIG_CHIP_PRE_INIT
/* Default use UART1 as console */
#define CONFIG_CONSOLE_UART    0

#define GPIO_PIN(port, index) GPIO_##port, BIT(index)
#define GPIO_PIN_MASK(p, m) .port = GPIO_##p, .mask = (m)

#endif  /* __CROS_EC_CONFIG_CHIP_H */
