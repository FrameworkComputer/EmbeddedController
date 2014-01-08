/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"

/* Default to UART 1 for EC console */
#define CONFIG_UART_CONSOLE 1

/* Use variant specific configuration for flash / UART / IRQ */
#if defined(CHIP_VARIANT_STM32L15X)
#include "config-stm32l15x.h"
#elif defined(CHIP_VARIANT_STM32L100)
#include "config-stm32l100.h"
#elif defined(CHIP_VARIANT_STM32F100)
/* STM32F100xx is currently the only outlier in the STM32F series */
#include "config-stm32f100.h"
#elif defined(CHIP_VARIANT_STM32F10X)
/* STM32F101xx, STM32F102xx, STM32F103xx, STM32F105xx, and STM32F107xx */
#include "config-stm32f10x.h"
#else
#error "Unsupported chip variant"
#endif

/* System stack size */
#define CONFIG_STACK_SIZE 1024

/* Idle task stack size */
#define IDLE_TASK_STACK_SIZE 256

/* Default task stack size */
#define TASK_STACK_SIZE 488

/* Larger task stack size, for hook task */
#define LARGER_TASK_STACK_SIZE 640

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL (500 * MSEC)

/* Maximum number of deferrable functions */
#define DEFERRABLE_MAX_COUNT 8

/* Number of I2C ports */
#define I2C_PORT_COUNT 2

/*
 * Use a timer to print a watchdog warning event before the actual watchdog
 * timer fires.  This is needed on STM32, where the independent watchdog has no
 * early warning feature and the windowed watchdog has a very short period.
 */
#define CONFIG_WATCHDOG_HELP

/* Use DMA */
#define CONFIG_DMA

/*
 * Use DMA for UART transmit for all platforms.  DMA for UART receive is
 * enabled on a per-chip basis because it doesn't seem to work reliably on
 * STM32F (see crosbug.com/p/24141).
 */
#define CONFIG_UART_TX_DMA

/* Flash protection applies to the next boot, not the current one */
#define CONFIG_FLASH_PROTECT_NEXT_BOOT

/* Compile for running from RAM instead of flash */
/* #define COMPILE_FOR_RAM */

#endif /* __CROS_EC_CONFIG_CHIP_H */
