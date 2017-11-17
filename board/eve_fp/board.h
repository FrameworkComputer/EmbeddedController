/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint microcontroller configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_SYSTEM_UNLOCKED

/* Serial console */
#define CONFIG_UART_CONSOLE 1
#define CONFIG_UART_TX_DMA_CH STM32_DMAC_USART1_TX
#define CONFIG_UART_TX_DMA_PH 2

/* Optional features */
#define CONFIG_CMD_SPI_XFER
#define CONFIG_HOST_COMMAND_STATUS
#undef  CONFIG_LID_SWITCH
#define CONFIG_MKBP_EVENT
#define CONFIG_FPU
#define CONFIG_SPI
#define CONFIG_SPI_MASTER
#define CONFIG_STM_HWTIMER32
#define CONFIG_WATCHDOG_HELP

/* Fingerprint configuration */
#define CONFIG_SPI_FP_PORT    1  /* SPI3: second SPI master port */
#define CONFIG_FP_SENSOR_FPC1145
#define CONFIG_CMD_FPSENSOR_DEBUG

/* Timer selection */
#define TIM_CLOCK32     2
#define TIM_WATCHDOG   16

/*
 * PLL configuration for 80Mhz:
 * SYSCLK = HSE * n/m/r = 16 * 10 / 2 = 80 Mhz
 */
#undef STM32_PLLM
#define STM32_PLLM	1
#undef STM32_PLLN
#define STM32_PLLN	10
#undef STM32_PLLR
#define STM32_PLLR	2

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

void fps_event(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
