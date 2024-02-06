/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nucleo-F412ZG baseboard configuration */

#ifndef __CROS_EC_BASE_BOARD_H
#define __CROS_EC_BASE_BOARD_H

/*-------------------------------------------------------------------------*
 * Flash layout:
 *
 * +++++++++++++
 * |    RO     |
 * | ......... |
 * |  Rollback | (two sectors)
 * +-----------+
 * |    RW     |
 * |           |
 * |           |
 * |           |
 * |           |
 * +++++++++++++
 *
 * We adjust the following macros to accommodate a for a rollback, RO,
 * and RW region of different sizes.
 *
 *-------------------------------------------------------------------------*/

#undef _IMAGE_SIZE
#undef CONFIG_ROLLBACK_OFF
#undef CONFIG_ROLLBACK_SIZE
#undef CONFIG_FLASH_PSTATE
#undef CONFIG_FW_PSTATE_SIZE
#undef CONFIG_FW_PSTATE_OFF
#undef CONFIG_SHAREDLIB_SIZE
#undef CONFIG_RO_MEM_OFF
#undef CONFIG_RO_STORAGE_OFF
#undef CONFIG_RO_SIZE
#undef CONFIG_RW_MEM_OFF
#undef CONFIG_RW_STORAGE_OFF
#undef CONFIG_RW_SIZE
#undef CONFIG_EC_PROTECTED_STORAGE_OFF
#undef CONFIG_EC_PROTECTED_STORAGE_SIZE
#undef CONFIG_EC_WRITABLE_STORAGE_OFF
#undef CONFIG_EC_WRITABLE_STORAGE_SIZE
#undef CONFIG_WP_STORAGE_OFF
#undef CONFIG_WP_STORAGE_SIZE

#define CONFIG_FLASH_WRITE_SIZE STM32_FLASH_WRITE_SIZE_3300

#define CONFIG_SHAREDLIB_SIZE 0

#define CONFIG_RO_MEM_OFF 0
#define CONFIG_RO_STORAGE_OFF 0
#define CONFIG_RO_SIZE (128 * 1024)

/* EC rollback protection block */
#define CONFIG_ROLLBACK_OFF (CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE)
#define CONFIG_ROLLBACK_SIZE (128 * 1024 * 2) /* 2 blocks of 128KB each */

#define CONFIG_RW_MEM_OFF (CONFIG_ROLLBACK_OFF + CONFIG_ROLLBACK_SIZE)
#define CONFIG_RW_STORAGE_OFF 0
#define CONFIG_RW_SIZE \
	(CONFIG_FLASH_SIZE_BYTES - (CONFIG_RW_MEM_OFF - CONFIG_RO_MEM_OFF))

#define CONFIG_EC_PROTECTED_STORAGE_OFF CONFIG_RO_MEM_OFF
#define CONFIG_EC_PROTECTED_STORAGE_SIZE CONFIG_RO_SIZE
#define CONFIG_EC_WRITABLE_STORAGE_OFF CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_SIZE CONFIG_RW_SIZE

#define CONFIG_WP_STORAGE_OFF CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE CONFIG_EC_PROTECTED_STORAGE_SIZE

/*
 * We want to prevent flash readout, and use it as indicator of protection
 * status.
 */
#define CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE

/*-------------------------------------------------------------------------*
 * UART Console Setup
 *-------------------------------------------------------------------------*/

/* The UART console is on USART3 */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 3
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 2048
#define CONFIG_UART_TX_REQ_CH 4
#define CONFIG_UART_RX_REQ_CH 4
/* We don't currently use DMA. */
#define CONFIG_UART_TX_DMA_PH DMAMUX1_REQ_USART3_TX
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA

/*-------------------------------------------------------------------------*
 * Console Commands
 *-------------------------------------------------------------------------*/

#define CONFIG_CMD_FLASH
#define CONFIG_CMD_IDLE_STATS

/*-------------------------------------------------------------------------*
 * Rollback Block
 *-------------------------------------------------------------------------*/

#define CONFIG_ROLLBACK
#define CONFIG_MPU
#define CONFIG_ROLLBACK_MPU_PROTECT

/*
 * We do not use any "locally" generated entropy: this is normally used
 * to add local entropy when the main source of entropy is remote.
 */
#undef CONFIG_ROLLBACK_SECRET_LOCAL_ENTROPY_SIZE
#ifdef SECTION_IS_RW
#undef CONFIG_ROLLBACK_UPDATE
#endif

/*-------------------------------------------------------------------------*
 * RW Signature Verification
 *-------------------------------------------------------------------------*/

#ifdef SECTION_IS_RO
/* RO verifies the RW partition signature */
#define CONFIG_RSA
#define CONFIG_RWSIG
#endif /* SECTION_IS_RO */
#define CONFIG_RSA_KEY_SIZE 3072
#define CONFIG_RSA_EXPONENT_3
#define CONFIG_RWSIG_TYPE_RWSIG

/*-------------------------------------------------------------------------*
 * Watchdog
 *-------------------------------------------------------------------------*/

/*
 * RW does slow compute, RO does slow flash erase.
 */
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 10000
#define CONFIG_WATCHDOG_HELP

/*-------------------------------------------------------------------------*
 * Disable Features
 *-------------------------------------------------------------------------*/

#undef CONFIG_ADC
#undef CONFIG_HIBERNATE
#undef CONFIG_I2C
#undef CONFIG_LID_SWITCH
#undef CONFIG_TASK_PROFILING

/*-------------------------------------------------------------------------*
 * Other
 *-------------------------------------------------------------------------*/

#define CONFIG_BORINGSSL_CRYPTO
#define CONFIG_DMA_CROS
#define CONFIG_FPU
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO
#define CONFIG_PRINTF_LONG_IS_32BITS
#define CONFIG_RNG
#define CONFIG_SHA256_SW
#define CONFIG_SHA256_UNROLLED
#define CONFIG_WP_ACTIVE_HIGH

#ifndef TEST_BUILD
/* TODO(hesling): Fix the illogical dependency between spi.c
 *                and host_command.c
 *
 * Currently, the chip/stm32/spi.c depends on functions defined in
 * common/host_command.c. When unit test builds use their own tasklist
 * without the HOSTCMD task, host_command.c is excluded from the build,
 * but chip/stm32/spi.c remains (because of CONFIG_SPI).
 * This triggers an undefined reference linker error.
 * The reproduce case:
 * - Allow CONFIG_SPI in TEST_BUILDs
 * - make BOARD=nucleo-h743zi tests
 */
#define CONFIG_SPI
#endif

#ifndef __ASSEMBLER__
/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_WATCHDOG 16
#include "gpio_signal.h"
void button_event(enum gpio_signal signal);
#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASE_BOARD_H */
