/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * STM32H743 + FPC 1145 Fingerprint MCU configuration
 *
 * Alternate names that share this same board file:
 *   nocturne_fp
 *   nami_fp
 *   dartmonkey
 *   dragontalon
 */

#ifndef __BOARD_H
#define __BOARD_H

#undef CONFIG_SYSTEM_UNLOCKED

#define CONFIG_ALLOW_UNALIGNED_ACCESS
#define CONFIG_LTO

/*
 * These allow console commands to be flagged as restricted.
 * Restricted commands will only be permitted to run when
 * console_is_restricted() returns false.
 * See console_is_restricted's definition in board.c.
 */
#define CONFIG_CONSOLE_COMMAND_FLAGS
#define CONFIG_RESTRICTED_CONSOLE_COMMANDS

/*
 * Flash layout: we redefine the sections offsets and sizes as we want to
 * include a rollback region, and will use RO/RW regions of different sizes.
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

#define CONFIG_SHAREDLIB_SIZE 0

#define CONFIG_RO_MEM_OFF 0
#define CONFIG_RO_STORAGE_OFF 0
#define CONFIG_RO_SIZE (768 * 1024)

/* EC rollback protection block */
#define CONFIG_ROLLBACK_OFF (CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE)
#define CONFIG_ROLLBACK_SIZE (CONFIG_FLASH_BANK_SIZE * 2)

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

/* the UART console is on USART1 */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1
#define CONFIG_UART_TX_DMA
#define CONFIG_UART_TX_DMA_PH DMAMUX1_REQ_USART1_TX
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 2048

/* Optional features */
#undef CONFIG_ADC
#define CONFIG_CMD_IDLE_STATS
#define CONFIG_DMA_CROS
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_FPU
/*
 * Don't enable FPU warnings on STM32H7, because FPU interrupt is not triggered
 * (see, errata ES0392 Rev 8, 2.1.2 Cortex-M7 FPU interrupt not present on NVIC
 * line 81).
 */
#undef CONFIG_FPU_WARNINGS
#undef CONFIG_HIBERNATE
#define CONFIG_HOST_COMMAND_STATUS
#undef CONFIG_I2C
#undef CONFIG_LID_SWITCH
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO
#define CONFIG_PRINTF_LONG_IS_32BITS
#define CONFIG_SHA256_SW
#define CONFIG_SHA256_UNROLLED
#define CONFIG_SPI
#undef CONFIG_TASK_PROFILING
#define CONFIG_WATCHDOG_HELP
#define CONFIG_WP_ACTIVE_HIGH
#define CONFIG_PANIC_STRIP_GPR

#if defined(BOARD_NOCTURNE_FP) || defined(BOARD_NAMI_FP)
/*
 * FPMCU RO for nocturne (nocturne_fp_v2.2.64-58cf5974e) and
 * FPMCU RO for nami (nami_fp_v2.2.144-7a08e07eb)
 * don't have the RV32I core panic data in their panic data structure.
 * As a consequence the size of panic data structure is different between RO
 * and RW (RO panic data structure is smaller). This results in overwriting RW
 * panic data (if it exists) by RO when jumping to RW. Another problem is that
 * RW can't find the jump data, because owerwritten panic data structure created
 * by RW still contains RW panic data structure size (bigger than RO's), so
 * calculated jump data address is wrong.
 *
 * The problem is fixed by excluding RV32I core panic data from RW, only when
 * compiling firmware for nami_fp and nocturne_fp. Expected size of the
 * structure is 116 bytes.
 */
#define CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA
#define CONFIG_RO_PANIC_DATA_SIZE 116

/*
 * FPMCU RO for nocturne and nami don't support extended reset flags in backup
 * RAM. RO interprets the register as saved panic reason, so it tries to restore
 * panic data when value in the register is different than 0. Another problem is
 * that panic data saved in backup RAM on hard reset is restored incorrectly.
 *
 * CL:1295890 is the change which introduced extended reset flags.
 */
#undef CONFIG_STM32_EXTENDED_RESET_FLAGS
#else
/*
 * Dartmonkey FPMCU RO (dartmonkey_v2.0.2887-311310808) has RV32I core panic
 * data structure in their panic data structure, so expected size of the
 * structure is 144 bytes.
 */
#define CONFIG_RO_PANIC_DATA_SIZE 144
#endif /* defined(BOARD_NOCTURNE_FP) || defined(BOARD_NAMI_FP) */

/* SPI configuration for the fingerprint sensor */
#define CONFIG_SPI_CONTROLLER
#define CONFIG_SPI_FP_PORT 2 /* SPI4: third master config */
#define CONFIG_SPI_MUTABLE_DEVICE_LIST

#define CONFIG_FINGERPRINT_MCU
#ifdef SECTION_IS_RW
#define CONFIG_FP_SENSOR_FPC1145
#define CONFIG_CMD_FPSENSOR_DEBUG
/* Special memory regions to store large arrays */
#define FP_FRAME_SECTION __SECTION(ahb4)
#define FP_TEMPLATE_SECTION __SECTION(ahb)

#else /* SECTION_IS_RO */
/* RO verifies the RW partition signature */
#define CONFIG_RSA
#define CONFIG_RWSIG
#endif

#define CONFIG_RSA_KEY_SIZE 3072
#define CONFIG_RSA_EXPONENT_3
#define CONFIG_RWSIG_TYPE_RWSIG

/* RW does slow compute, RO does slow flash erase. */
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 10000

/*
 * Add rollback protection
 */
#define CONFIG_ROLLBACK
#define CONFIG_ROLLBACK_SECRET_SIZE 32

#define CONFIG_ROLLBACK_MPU_PROTECT

/*
 * We do not use any "locally" generated entropy: this is normally used
 * to add local entropy when the main source of entropy is remote.
 */
#undef CONFIG_ROLLBACK_SECRET_LOCAL_ENTROPY_SIZE
#ifdef SECTION_IS_RW
#undef CONFIG_ROLLBACK_UPDATE
#endif

#define CONFIG_BORINGSSL_CRYPTO

#define CONFIG_RNG

#define CONFIG_CMD_FLASH

#ifdef SECTION_IS_RW
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_CMD_SPI_XFER
#endif

#ifdef SECTION_IS_RW
/*
 * Mitigating the effects of b/146428434.
 */
#define APPLY_RESET_LOOP_FIX
#endif

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_WATCHDOG 16

#include "board_rw.h"
#include "gpio_signal.h"

void slp_event(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
