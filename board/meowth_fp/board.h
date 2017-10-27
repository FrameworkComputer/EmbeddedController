/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Meowth Fingerprint MCU configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* the UART console is on USART1 */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1
#define CONFIG_UART_TX_DMA
#define CONFIG_UART_TX_DMA_PH DMAMUX1_REQ_USART1_TX

/* Optional features */
#undef CONFIG_ADC
#define CONFIG_DMA
#define CONFIG_FPU
#undef CONFIG_HIBERNATE
#undef CONFIG_I2C
#undef CONFIG_LID_SWITCH
#define CONFIG_MKBP_EVENT
#define CONFIG_PRINTF_LEGACY_LI_FORMAT
#define CONFIG_STM_HWTIMER32
#undef CONFIG_TASK_PROFILING
#define CONFIG_WATCHDOG_HELP
#define CONFIG_WP_ALWAYS

/* Temporary */
#undef CONFIG_FLASH
#undef CONFIG_FLASH_PHYSICAL

#define CONFIG_CMD_FLASH

/* SPI configuration for the fingerprint sensor */
#define CONFIG_SPI_MASTER
#define CONFIG_SPI_FP_PORT  2 /* SPI4: third master config */
#define CONFIG_FP_SENSOR_FPC1145
#define CONFIG_CMD_FPSENSOR_DEBUG

#define CONFIG_CMD_SPI_XFER

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_WATCHDOG 16

#include "gpio_signal.h"

void fps_event(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
