/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Tiny charger configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#define CONFIG_UART_CONSOLE 1

/* Optional features */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_TASK_PROFILING
#undef CONFIG_COMMON_GPIO
#undef CONFIG_COMMON_PANIC_OUTPUT
#undef CONFIG_COMMON_RUNTIME
#undef CONFIG_COMMON_TIMER
#undef CONFIG_CONSOLE_CMDHELP
#undef CONFIG_DEBUG_ASSERT
#undef CONFIG_DEBUG_EXCEPTIONS
#undef CONFIG_DEBUG_STACK_OVERFLOW
#undef CONFIG_FLASH
#undef CONFIG_FMAP

/* debug printf flash footprinf is about 1400 bytes */
#define CONFIG_DEBUG_PRINTF
#define UARTN CONFIG_UART_CONSOLE

#ifndef __ASSEMBLER__

/* No GPIO abstraction layer */
enum gpio_signal;

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
