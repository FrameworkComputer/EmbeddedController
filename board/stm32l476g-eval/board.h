/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32L476G-Eval board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#ifdef CTS_MODULE
/* CTS tests are small. We can use smaller size to expedite flash time. */
#undef CONFIG_FLASH_SIZE_BYTES
#define CONFIG_FLASH_SIZE_BYTES 0x00040000 /* 256k */
#endif

/* Optional features */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

/* Console is on LPUART (PG7/8). Undef it to use USART1 (PB6/7). */
#define STM32L476G_EVAL_USE_LPUART_CONSOLE
#undef CONFIG_UART_CONSOLE

#ifdef STM32L476G_EVAL_USE_LPUART_CONSOLE
#define CONFIG_UART_CONSOLE 9
#define CONFIG_UART_TX_DMA_CH STM32_DMAC_CH14
#define CONFIG_UART_TX_DMA_PH 4
#else
#define CONFIG_UART_CONSOLE 1
#define CONFIG_UART_TX_DMA_CH STM32_DMAC_USART1_TX
#define CONFIG_UART_TX_DMA_PH 2
#endif

/* Optional features */

#ifdef CTS_MODULE_I2C
#define CONFIG_I2C
#define CONFIG_I2C_PERIPHERAL
#define CONFIG_HOSTCMD_I2C_SLAVE_ADDR 0x3c
#define I2C_PORT_EC STM32_I2C2_PORT
#endif

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

#undef CONFIG_FLASH_CROS
#undef CONFIG_FLASH_PHYSICAL

/* Timer selection */
#define TIM_CLOCK32 5

/* External clock speeds (8 MHz) */
#define CONFIG_STM32_CLOCK_HSE_HZ 8000000

/* PLL configuration. Freq = CONFIG_STM32_CLOCK_HSE_HZ * n/m/r */
#define STM32_USE_PLL
#define STM32_INITIAL_PLL_INPUT OSC_HSE
#undef STM32_PLLM
#define STM32_PLLM 1
#undef STM32_PLLN
#define STM32_PLLN 10
#undef STM32_PLLR
#define STM32_PLLR 2

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
