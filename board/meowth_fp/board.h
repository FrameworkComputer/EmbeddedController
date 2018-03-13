/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Meowth Fingerprint MCU configuration */

#ifndef __BOARD_H
#define __BOARD_H

/*
 * TODO(b/73337313) remove this config,
 * once the write-protection scheme is decided and validated.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* the UART console is on USART1 */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1
#define CONFIG_UART_TX_DMA
#define CONFIG_UART_TX_DMA_PH DMAMUX1_REQ_USART1_TX
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 2048

/* Optional features */
#undef CONFIG_ADC
#define CONFIG_DMA
#define CONFIG_FPU
#undef CONFIG_HIBERNATE
#define CONFIG_HOST_COMMAND_STATUS
#undef CONFIG_I2C
#undef CONFIG_LID_SWITCH
#define CONFIG_MKBP_EVENT
#define CONFIG_PRINTF_LEGACY_LI_FORMAT
#define CONFIG_SHA256
#define CONFIG_SHA256_UNROLLED
#define CONFIG_SPI
#define CONFIG_STM_HWTIMER32
#define CONFIG_SUPPRESSED_HOST_COMMANDS \
	EC_CMD_CONSOLE_SNAPSHOT, EC_CMD_CONSOLE_READ, EC_CMD_PD_GET_LOG_ENTRY
#undef CONFIG_TASK_PROFILING
#define CONFIG_WATCHDOG_HELP
#define CONFIG_WP_ACTIVE_HIGH

/* SPI configuration for the fingerprint sensor */
#define CONFIG_SPI_MASTER
#define CONFIG_SPI_FP_PORT  2 /* SPI4: third master config */
#ifdef SECTION_IS_RW
#define CONFIG_FP_SENSOR_FPC1145
#define CONFIG_CMD_FPSENSOR_DEBUG
/*
 * Use the malloc code only in the RW section (for the private library),
 * we cannot enable it in RO since it is not compatible with the RW verification
 * (shared_mem_init done too late).
 */
#define CONFIG_MALLOC
/* Special memory regions to store large arrays */
#define FP_FRAME_SECTION    __SECTION(ahb4)
#define FP_TEMPLATE_SECTION __SECTION(ahb)

/* we are doing slow compute */
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 10000

#else /* SECTION_IS_RO */
/* RO verifies the RW partition signature */
#define CONFIG_RSA
#define CONFIG_RSA_KEY_SIZE 3072
#define CONFIG_RSA_EXPONENT_3
#define CONFIG_RWSIG
#endif
#define CONFIG_RWSIG_TYPE_RWSIG

#define CONFIG_CMD_FLASH
#define CONFIG_CMD_SPI_XFER

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_WATCHDOG 16

#include "gpio_signal.h"

void fps_event(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
