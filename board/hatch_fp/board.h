/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hatch Fingerprint MCU configuration */

#ifndef __BOARD_H
#define __BOARD_H

#define CONFIG_SYSTEM_UNLOCKED

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

#undef  CONFIG_RAM_SIZE
#define CONFIG_RAM_SIZE         0x40000 /* 256 KB */
#undef  CONFIG_FLASH_SIZE
#define CONFIG_FLASH_SIZE       (1 * 1024 * 1024)

#define CONFIG_FLASH_WRITE_SIZE STM32_FLASH_WRITE_SIZE_3300

#define CONFIG_SHAREDLIB_SIZE   0

#define CONFIG_RO_MEM_OFF       0
#define CONFIG_RO_STORAGE_OFF   0
#define CONFIG_RO_SIZE          (128 * 1024)

/* EC rollback protection block */
#define CONFIG_ROLLBACK_OFF (CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE)
#define CONFIG_ROLLBACK_SIZE (128 * 1024 * 2) /* 2 blocks of 128KB each */

#define CONFIG_RW_MEM_OFF	(CONFIG_ROLLBACK_OFF + CONFIG_ROLLBACK_SIZE)
#define CONFIG_RW_STORAGE_OFF	0
#define CONFIG_RW_SIZE		(CONFIG_FLASH_SIZE -			\
				(CONFIG_RW_MEM_OFF - CONFIG_RO_MEM_OFF))

#define CONFIG_EC_PROTECTED_STORAGE_OFF         CONFIG_RO_MEM_OFF
#define CONFIG_EC_PROTECTED_STORAGE_SIZE        CONFIG_RO_SIZE
#define CONFIG_EC_WRITABLE_STORAGE_OFF          CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_SIZE         CONFIG_RW_SIZE

#define CONFIG_WP_STORAGE_OFF           CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE          CONFIG_EC_PROTECTED_STORAGE_SIZE

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
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA
#define CONFIG_UART_TX_REQ_CH 4
#define CONFIG_UART_RX_REQ_CH 4

/* Optional features */
#undef CONFIG_ADC
#define CONFIG_CMD_IDLE_STATS
#define CONFIG_DMA
/*FIXME*/
/*#define CONFIG_FORCE_CONSOLE_RESUME*/
#define CONFIG_FPU
#undef CONFIG_HIBERNATE
#define CONFIG_HOST_COMMAND_STATUS
#undef CONFIG_I2C
#undef CONFIG_LID_SWITCH
/*FIXME*/
/*#define CONFIG_LOW_POWER_IDLE*/
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO
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
#define CONFIG_SPI_FP_PORT  0 /* SPI2: first master config */
#ifdef SECTION_IS_RW
#define CONFIG_FP_SENSOR_FPC1025
/* TODO(b/130249462): remove for release */
#define CONFIG_CMD_FPSENSOR_DEBUG
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

#else /* SECTION_IS_RO */
/* RO verifies the RW partition signature */
#define CONFIG_RSA
#define CONFIG_RSA_KEY_SIZE 3072
#define CONFIG_RSA_EXPONENT_3
#define CONFIG_RWSIG
#endif
#define CONFIG_RWSIG_TYPE_RWSIG

/* RW does slow compute, RO does slow flash erase. */
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 10000

/*
 * Add rollback protection
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

#define CONFIG_AES
#define CONFIG_AES_GCM

#define CONFIG_RNG

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
