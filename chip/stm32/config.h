/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CHIP_CONFIG_H
#define __CROS_EC_CHIP_CONFIG_H

/* use variant specific configuration for flash / UART / IRQ */
#if defined(CHIP_VARIANT_stm32l15x)
#include "config-stm32l15x.h"
#elif defined(CHIP_VARIANT_stm32f100)
#include "config-stm32f100.h"
#else
#error "Unsupported chip variant"
#endif

/* System stack size */
#define CONFIG_STACK_SIZE 1024

/* Idle task stack size */
#define IDLE_TASK_STACK_SIZE 256

/* Default task stack size */
#define TASK_STACK_SIZE 488

/* support programming on-chip flash */
#define CONFIG_FLASH

/* build with assertions and debug messages */
#define CONFIG_DEBUG

/* Compile for running from RAM instead of flash */
/* #define COMPILE_FOR_RAM */

#endif /* __CROS_EC_CHIP_CONFIG_H */
