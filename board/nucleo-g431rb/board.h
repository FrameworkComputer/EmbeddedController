/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32G431 Nucleo-64 board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

#define CPU_CLOCK 48000000
#define CONFIG_STM_HWTIMER32
#define TIM_CLOCK32 2
#define TIM_CLOCK_MSB  3
#define TIM_CLOCK_LSB 15
#define TIM_WATCHDOG 7

/* Nucelo platform does not have a lid switch */
#undef CONFIG_LID_SWITCH


/* Setup UART console */
/*
 * The STM32G431 Nucleo-64 has two UARTs which can be connected to the virtual
 * com port(VCP) of the STLINK chip. The VCP to STM32G4 connection depends on
 * solder bridge configuration. The default configuration is VCP to LUPAURT
 * (PA2/PA3). In order to reuse existing stm32 uart drivers, UART9 is used to
 * indicate that lpuart is being used.
 *
 * The STM32G4 has a DMAMUX and so both the DMA channel and DMAMUX request
 * numbers need to be specified here.
 */
#define STM32G431_EVAL_USE_LPUART_CONSOLE
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_TX_DMA
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 2048

#ifdef STM32G431_EVAL_USE_LPUART_CONSOLE
#define CONFIG_UART_CONSOLE 9
#define CONFIG_UART_TX_DMA_CH STM32_DMAC_LPUART_TX
#define CONFIG_UART_TX_DMA_PH DMAMUX_REQ_UART9_TX
#else
#define CONFIG_UART_CONSOLE 1
#define CONFIG_UART_TX_DMA_CH STM32_DMAC_USART1_TX
#define CONFIG_UART_TX_DMA_PH DMAMUX_REQ_USART1_TX
#endif


/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_ENTERING_RW	GPIO_EC_ENTERING_RW
#define GPIO_WP_L		GPIO_EC_WP_L



#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"


#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
