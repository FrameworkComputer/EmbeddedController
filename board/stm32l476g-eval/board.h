/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32L476G-Eval board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

/* the UART console is on USART1 (PB6/7) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1

/* Use USART1 for DMA TX */
#define CONFIG_UART_TX_DMA_CH STM32_DMAC_USART1_TX
#define CONFIG_UART_TX_DMA_PH 2

/* Optional features */
#define CONFIG_STM_HWTIMER32

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

#undef CONFIG_FLASH

/* Timer selection */
#define TIM_CLOCK32 5

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
