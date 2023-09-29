/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * STM32F412 + FPC 1025 Fingerprint MCU configuration
 *
 * Alternate names that share this same board file:
 *   hatch_fp
 *   bloonchipper
 *   dragonclaw
 */

#ifndef __BOARD_H
#define __BOARD_H

#undef CONFIG_SYSTEM_UNLOCKED

#define CONFIG_ALLOW_UNALIGNED_ACCESS

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
 * USART Transport Setup
 *-------------------------------------------------------------------------*/
/* Enable USART host commands */
#define CONFIG_USART_HOST_COMMAND
/* Enable USART and USART1 stream */
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART1
/* Allocate and configure hw instance of USART */
#undef CONFIG_UART_HOST_COMMAND_HW
#define CONFIG_UART_HOST_COMMAND_HW usart1_hw
/* Set baud rate of USART */
#undef CONFIG_UART_HOST_COMMAND_BAUD_RATE
#define CONFIG_UART_HOST_COMMAND_BAUD_RATE 3000000

/*-------------------------------------------------------------------------*
 * UART Console Setup
 *-------------------------------------------------------------------------*/

/* The UART console is on USART2 */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 2048
/* We don't currently use DMA. */
#undef CONFIG_UART_TX_DMA_PH
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA
#undef CONFIG_UART_TX_REQ_CH
#undef CONFIG_UART_RX_REQ_CH

/*-------------------------------------------------------------------------*
 * Console Commands
 *-------------------------------------------------------------------------*/

#define CONFIG_CMD_FLASH
#define CONFIG_CMD_IDLE_STATS

#ifdef SECTION_IS_RW
#define CONFIG_CMD_SPI_XFER
/* TODO(b/130249462): remove for release */
#define CONFIG_CMD_FPSENSOR_DEBUG
#endif

/*
 * These allow console commands to be flagged as restricted.
 * Restricted commands will only be permitted to run when
 * console_is_restricted() returns false.
 * See console_is_restricted's definition in board.c.
 */
#define CONFIG_CONSOLE_COMMAND_FLAGS
#define CONFIG_RESTRICTED_CONSOLE_COMMANDS

/*-------------------------------------------------------------------------*
 * Rollback Block
 *-------------------------------------------------------------------------*/

#define CONFIG_ROLLBACK
#define CONFIG_ROLLBACK_SECRET_SIZE 32
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
 * Fingerprint Specific
 *-------------------------------------------------------------------------*/

/* SPI configuration for the fingerprint sensor */
#define CONFIG_SPI_CONTROLLER
#define CONFIG_SPI_FP_PORT 0 /* SPI2: first master config */
#define CONFIG_FINGERPRINT_MCU
#ifdef SECTION_IS_RW
#define CONFIG_FP_SENSOR_FPC1025
/*
 * Use the malloc code only in the RW section (for the private library),
 * we cannot enable it in RO since it is not compatible with the RW verification
 * (shared_mem_init done too late).
 */
#define CONFIG_MALLOC
/*
 * FP buffers are allocated in regular SRAM on STM32F4.
 * TODO(b/124773209): Instead of defining to empty, #undef once all CLs that
 * depend on FP_*_SECTION have landed. Also rename the variables to CONFIG_*.
 */
#define FP_FRAME_SECTION
#define FP_TEMPLATE_SECTION
#endif /* SECTION_IS_RW */

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
#define CONFIG_FPU_WARNINGS
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO
#define CONFIG_PRINTF_LONG_IS_32BITS
#define CONFIG_RNG
#define CONFIG_SHA256
#define CONFIG_SHA256_UNROLLED
#define CONFIG_SPI
#define CONFIG_WP_ACTIVE_HIGH
#define CONFIG_PANIC_STRIP_GPR

/*
 * Bloonchipper FPMCU RO has RV32I core panic data structure in their panic
 * data structure, so expected size of the structure is 144 bytes.
 */
#define CONFIG_RO_PANIC_DATA_SIZE 144

#ifdef SECTION_IS_RW
#define CONFIG_LOW_POWER_IDLE
#endif /* SECTION_IS_RW */

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_WATCHDOG 9

#include "board_rw.h"
#include "gpio_signal.h"

void slp_event(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
