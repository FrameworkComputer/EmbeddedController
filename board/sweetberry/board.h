/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Sweetberry configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Use external clock */
#define CONFIG_STM32_CLOCK_HSE_HZ 24000000

#define CONFIG_BOARD_POST_GPIO_INIT

/* Enable console recasting of GPIO type. */
#define CONFIG_CMD_GPIO_EXTENDED

/* The UART console can be on flax USART3 (PC10/PC11) */
/* The UART console can be on header USART4 (PA0/PA1) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 4
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096
/* Don't waste precious DMA channels on console. */
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA

#define CONFIG_UART_TX_REQ_CH 4
#define CONFIG_UART_RX_REQ_CH 4

#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define I2C_PORT_0	0
#define I2C_PORT_1	1
#define I2C_PORT_2	2
#define FMPI2C_PORT_3	3

/* This is not actually a Chromium EC so disable some features. */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_WATCHDOG

/* Optional features */
#define CONFIG_STM_HWTIMER32

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 5

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
