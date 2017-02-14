/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint microcontroller configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * TODO(crosbug.com/p/62893)
 * implement flash support for STM32L4
 * and write protection.
 */
#undef  CONFIG_FLASH
#undef  CONFIG_FLASH_PHYSICAL
#define CONFIG_SYSTEM_UNLOCKED

/* Serial console */
#define CONFIG_UART_CONSOLE 1
#define CONFIG_UART_TX_DMA_CH STM32_DMAC_USART1_TX
#define CONFIG_UART_TX_DMA_PH 2

/* Optional features */
#define CONFIG_CMD_SPI_XFER
#undef  CONFIG_LID_SWITCH
#define CONFIG_SPI
#define CONFIG_SPI_MASTER
#define CONFIG_STM_HWTIMER32


#define CONFIG_SPI_FP_PORT    1  /* SPI3: second SPI master port */

/* Timer selection */
#define TIM_CLOCK32     2
#define TIM_WATCHDOG   16

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

void fps_event(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
