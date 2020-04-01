/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

#ifdef CHIP_FAMILY_STM32F0
/* CPU core BFD configuration */
#include "core/cortex-m0/config_core.h"
/* IRQ priorities */
#define STM32_IRQ_EXT0_1_PRIORITY	1
#define STM32_IRQ_EXT2_3_PRIORITY	1
#define STM32_IRQ_EXTI4_15_PRIORITY	1
#else
/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"
#endif

/* Default to UART 1 for EC console */
#define CONFIG_UART_CONSOLE 1

/* Use variant specific configuration for flash / UART / IRQ */
/* STM32F03X8 it itself a variant of STM32F03X with non-default flash sizes */
#ifdef CHIP_VARIANT_STM32F03X8
#define CHIP_VARIANT_STM32F03X
#endif

/* Number of I2C ports, can be overridden in variant */
#define I2C_PORT_COUNT 2

#if defined(CHIP_VARIANT_STM32L476)
#include "config-stm32l476.h"
#elif defined(CHIP_VARIANT_STM32L15X)
#include "config-stm32l15x.h"
#elif defined(CHIP_VARIANT_STM32L100)
#include "config-stm32l100.h"
#elif defined(CHIP_VARIANT_STM32L442)
#include "config-stm32l442.h"
#elif defined(CHIP_VARIANT_STM32F76X)
#include "config-stm32f76x.h"
#elif defined(CHIP_FAMILY_STM32F4)
/* STM32F4 family */
#include "config-stm32f4.h"
#elif defined(CHIP_VARIANT_STM32F373)
#include "config-stm32f373.h"
#elif defined(CHIP_VARIANT_STM32F09X)
/* STM32F09xx */
#include "config-stm32f09x.h"
#elif defined(CHIP_VARIANT_STM32F07X) || defined(CHIP_VARIANT_STM32F070)
/* STM32F07xx */
#include "config-stm32f07x.h"
#elif defined(CHIP_VARIANT_STM32F05X)
/* STM32F05xx */
#include "config-stm32f05x.h"
#elif defined(CHIP_VARIANT_STM32F03X)
/* STM32F03x */
#include "config-stm32f03x.h"
#elif defined(CHIP_VARIANT_STM32H7X3)
#include "config-stm32h7x3.h"
#elif defined(CHIP_VARIANT_STM32G431XB)
#include "config-stm32g41xb.h"
#else
#error "Unsupported chip variant"
#endif

#define CONFIG_PROGRAM_MEMORY_BASE 0x08000000

/* Memory-mapped internal flash */
#define CONFIG_INTERNAL_STORAGE
#define CONFIG_MAPPED_STORAGE

/* Program is run directly from storage */
#define CONFIG_MAPPED_STORAGE_BASE CONFIG_PROGRAM_MEMORY_BASE

#if !defined(CHIP_FAMILY_STM32F4) && \
	!defined(CHIP_FAMILY_STM32F7) && \
	!defined(CHIP_FAMILY_STM32H7) && \
	!defined(CHIP_VARIANT_STM32F09X)
/* Compute the rest of the flash params from these */
#include "config_std_internal_flash.h"
#endif

/* Additional special purpose regions (USB RAM and other special SRAMs) */
#define CONFIG_CHIP_MEMORY_REGIONS

/* System stack size */
#if defined(CHIP_VARIANT_STM32F05X)
#define CONFIG_STACK_SIZE 768
#else
#define CONFIG_STACK_SIZE 1024
#endif

/* Idle task stack size */
#define IDLE_TASK_STACK_SIZE 256

/* Smaller task stack size */
#define SMALLER_TASK_STACK_SIZE 384

/* Default task stack size */
#define TASK_STACK_SIZE 512

/* Larger task stack size, for hook task */
#define LARGER_TASK_STACK_SIZE 640

/* Even bigger */
#define VENTI_TASK_STACK_SIZE 768
#define ULTRA_TASK_STACK_SIZE 1056
#define TRENTA_TASK_STACK_SIZE 1184

/*
 * Console stack size. For test builds, the console is used to interact with
 * the test, and insufficient stack size causes console stack overflow after
 * running the on-device tests.
 */
#define CONSOLE_TASK_STACK_SIZE 4096

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 500
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/*
 * Use a timer to print a watchdog warning event before the actual watchdog
 * timer fires.  This is needed on STM32, where the independent watchdog has no
 * early warning feature and the windowed watchdog has a very short period.
 */
#define CONFIG_WATCHDOG_HELP

/* Use DMA */
#define CONFIG_DMA

/* STM32 features RTC (optional feature) */
#define CONFIG_RTC

/* Number of peripheral request signals per DMA channel */
#define STM32_DMA_PERIPHERALS_PER_CHANNEL	4

/*
 * Use DMA for UART transmit for all platforms.  DMA for UART receive is
 * enabled on a per-chip basis because it doesn't seem to work reliably on
 * STM32F (see crosbug.com/p/24141).
 */
#define CONFIG_UART_TX_DMA

#ifndef CHIP_FAMILY_STM32H7
/* Flash protection applies to the next boot, not the current one */
#define CONFIG_FLASH_PROTECT_NEXT_BOOT
#endif /* !CHIP_FAMILY_STM32H7 */

/* Chip needs to do custom pre-init */
#define CONFIG_CHIP_PRE_INIT

#define GPIO_NAME_BY_PIN(port, index) #port#index
#define GPIO_PIN(port, index) GPIO_##port, BIT(index)
#define GPIO_PIN_MASK(p, m) .port = GPIO_##p, .mask = (m)

/* Prescaler values for PLL. Currently used only by STM32L476. */
#define STM32_PLLM	0
#define STM32_PLLN	0
#define STM32_PLLR	0

#endif /* __CROS_EC_CONFIG_CHIP_H */
