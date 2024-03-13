/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helipilot baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_HELIPILOT_BASE_BOARD_H
#define __CROS_EC_BASEBOARD_HELIPILOT_BASE_BOARD_H

#include "compile_time_macros.h"

#undef CONFIG_SYSTEM_UNLOCKED

#define CONFIG_ALLOW_UNALIGNED_ACCESS
#define CONFIG_LTO

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
 * We adjust the following macros to accommodate for a rollback, RO,
 * and RW region of different sizes.
 *
 *-------------------------------------------------------------------------*
 */

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

/*-------------------------------------------------------------------------*
 * Patch for reducing program memory size to increase data RAM
 *-------------------------------------------------------------------------*
 */
#undef NPCX_PROGRAM_MEMORY_SIZE
/* 352 KB program RAM */
#define NPCX_PROGRAM_MEMORY_SIZE ((416 - 64) * 1024)

#undef CONFIG_PROGRAM_MEMORY_BASE
#define CONFIG_PROGRAM_MEMORY_BASE 0x10058000

#undef CONFIG_RAM_BASE
/*
 * Adjust the base address of the Data RAM
 * 0x200C0000 - 64K (0x10000) memory address of Data RAM
 */
#define CONFIG_RAM_BASE 0x200B0000

#undef CONFIG_DATA_RAM_SIZE
/*
 * Define Data RAM size  = 160KB - 4KB (Reserved for booter).
 */
#define CONFIG_DATA_RAM_SIZE ((96 + 64) * 1024)

#undef CONFIG_RAM_SIZE
#define CONFIG_RAM_SIZE (CONFIG_DATA_RAM_SIZE - 0x1000)
/*-------------------------------------------------------------------------*/

#define CONFIG_SHAREDLIB_SIZE 0

#define CONFIG_RO_MEM_OFF 0

/* Need to account for the 64 (0x40) byte long firmware header */
#define CONFIG_RO_STORAGE_OFF 64
#define CONFIG_RO_SIZE (128 * 1024 - 0x1000)

#undef CONFIG_CODE_RAM_SIZE
#define CONFIG_CODE_RAM_SIZE NPCX_PROGRAM_MEMORY_SIZE

#define CONFIG_RO_PUBKEY_READ_ADDR                                      \
	(CONFIG_MAPPED_STORAGE_BASE + CONFIG_EC_PROTECTED_STORAGE_OFF + \
	 CONFIG_RO_PUBKEY_STORAGE_OFF)

#define CONFIG_RWSIG_READ_ADDR                                          \
	((CONFIG_MAPPED_STORAGE_BASE + CONFIG_EC_WRITABLE_STORAGE_OFF + \
	  CONFIG_RW_STORAGE_OFF + RW_SIG_OFFSET))
/*
 * Since NPCX9 executes out of SRAM and only one image (RO/RW) is loaded
 * from flash at a time, we don't apply an offset to program memory
 */
#define CONFIG_RW_MEM_OFF 0
#define CONFIG_RW_STORAGE_OFF 0

/*
 * The remaining flash size (CONFIG_FLASH_SIZE_BYTES -
 * (CONFIG_RW_MEM_OFF - CONFIG_RO_MEM_OFF) exceeds available program memory,
 * can only execute a program as big as the available program SRAM
 */
#define CONFIG_RW_SIZE                                          \
	((NPCX_PROGRAM_MEMORY_SIZE / CONFIG_FLASH_ERASE_SIZE) * \
	 CONFIG_FLASH_ERASE_SIZE)

#define CONFIG_EC_PROTECTED_STORAGE_OFF CONFIG_RO_MEM_OFF
#define CONFIG_EC_PROTECTED_STORAGE_SIZE (CONFIG_RO_SIZE + 0x1000)
#define CONFIG_EC_WRITABLE_STORAGE_OFF \
	(CONFIG_ROLLBACK_OFF + CONFIG_ROLLBACK_SIZE)

#define CONFIG_EC_WRITABLE_STORAGE_SIZE CONFIG_RW_SIZE

#define CONFIG_WP_STORAGE_OFF CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE CONFIG_EC_PROTECTED_STORAGE_SIZE
#define CONFIG_WP_ACTIVE_HIGH

/*
 * We want to prevent flash readout, and use it as indicator of protection
 * status.
 */
#define CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE

/*-------------------------------------------------------------------------*
 * Console Defines
 *-------------------------------------------------------------------------*
 */

/* Select which UART Controller is the Console UART */
#undef CONFIG_CONSOLE_UART
#define CONFIG_CONSOLE_UART 0 /* 0:UART1 1:UART2 */
#define NPCX_UART_MODULE2 1 /* 1:GPIO64/65 as UART1 */
#undef CONFIG_CONSOLE_IN_USE_ON_BOOT_TIME
#define CONFIG_CONSOLE_IN_USE_ON_BOOT_TIME 0

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 2048

#undef CONSOLE_TASK_STACK_SIZE
#define CONSOLE_TASK_STACK_SIZE 4096

/*-------------------------------------------------------------------------*
 * UART Host Command Interface Defines
 *-------------------------------------------------------------------------*
 */
#define NPCX_UART_BAUDRATE_3M

#undef CONFIG_UART_HOST_COMMAND_HW
#define CONFIG_UART_HOST_COMMAND_HW 1

#define CONFIG_USART_HOST_COMMAND

/*-------------------------------------------------------------------------*
 * Disable Features
 *-------------------------------------------------------------------------*
 */
#undef CONFIG_ADC
#undef CONFIG_CMD_ADC
#undef CONFIG_CMD_POWER_AP
#undef CONFIG_HIBERNATE
#undef CONFIG_LID_SWITCH
#undef CONFIG_SWITCH
#undef CONFIG_TEMP_SENSOR
#undef CONFIG_I2C
#undef CONFIG_TASK_PROFILING

/*-------------------------------------------------------------------------*
 * Enable Features
 *-------------------------------------------------------------------------*
 */
#define CONFIG_BORINGSSL_CRYPTO
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_IDLE_STATS
#define CONFIG_FPU
#define CONFIG_FPU_WARNINGS
#define CONFIG_HOST_INTERFACE_SHI
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO
#define CONFIG_PANIC_STRIP_GPR
#define CONFIG_PRINTF_LONG_IS_32BITS
#define CONFIG_RNG
#ifdef SECTION_IS_RW
#define CONFIG_SPI
#define CONFIG_CMD_SPI_XFER
/* TODO(b/130249462): remove for release */
#define CONFIG_CMD_FPSENSOR_DEBUG
#define CONFIG_LOW_POWER_IDLE
#endif

/*-------------------------------------------------------------------------*
 * Watchdog
 *-------------------------------------------------------------------------*
 */

/*
 * RW does slow compute, RO does slow flash erase.
 */
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 10000
#define CONFIG_WATCHDOG_HELP

/*-------------------------------------------------------------------------*
 * Fingerprint Specific
 *-------------------------------------------------------------------------*
 */

/* SPI configuration for the fingerprint sensor */
#define CONFIG_SPI_CONTROLLER
#define CONFIG_SPI_FP_PORT 0 /* SPI0: only one SPIP (SPI Peripheral) */
#define CONFIG_FINGERPRINT_MCU
#ifdef SECTION_IS_RW
/*
 * FP buffers are allocated in regular SRAM
 * TODO(b/124773209): Instead of defining to empty, #undef once all CLs that
 * depend on FP_*_SECTION have landed. Also rename the variables to CONFIG_*.
 */
#define FP_FRAME_SECTION
#define FP_TEMPLATE_SECTION
#endif /* SECTION_IS_RW */

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
 *-------------------------------------------------------------------------*
 */

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

/* EC rollback protection block */
#define CONFIG_ROLLBACK_OFF \
	(CONFIG_EC_PROTECTED_STORAGE_OFF + CONFIG_EC_PROTECTED_STORAGE_SIZE)
#define CONFIG_ROLLBACK_SIZE (128 * 1024 * 2) /* 2 blocks of 128KB each */

/*-------------------------------------------------------------------------*
 * RW Signature Verification
 *-------------------------------------------------------------------------*
 */

#ifdef SECTION_IS_RO
/* RO verifies the RW partition signature */
#define CONFIG_RSA
#define CONFIG_RWSIG
#endif /* SECTION_IS_RO */
#define CONFIG_RSA_KEY_SIZE 3072
#define CONFIG_RSA_EXPONENT_3
#define CONFIG_RWSIG_TYPE_RWSIG

/*-------------------------------------------------------------------------*
 * Chip Specific
 *-------------------------------------------------------------------------*
 */

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_WP GPIO_HOST_MCU_WP_OD
#define GPIO_SHI_CS_L GPIO_SPI_HOST_CS_MCU_ODL
#define GPIO_FPS_INT GPIO_FP_MCU_INT_L
#define GPIO_EC_INT_L GPIO_MCU_PLATFORM_INT_L

#ifndef __ASSEMBLER__

#include "base_board_rw.h"
#include "gpio_signal.h" /* needed by registers.h */
#include "registers.h"

void slp_event(enum gpio_signal signal);
#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_HELIPILOT_BASE_BOARD_H */
