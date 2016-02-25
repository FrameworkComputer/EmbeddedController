/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* glados_pd board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * The console task is too big to include in both RO and RW images. Therefore,
 * if the console task is defined, then only build an RW image. This can be
 * useful for debugging to have a full console. Otherwise, without this task,
 * a full RO and RW is built with a limited one-way output console.
 */
#ifdef HAS_TASK_CONSOLE
/*
 * The flash size is only 32kB.
 * No space for 2 partitions,
 * put only RW at the beginning of the flash
 */
#undef CONFIG_FW_INCLUDE_RO
#undef CONFIG_RW_MEM_OFF
#define CONFIG_RW_MEM_OFF 0
#undef CONFIG_RO_SIZE
#define CONFIG_RO_SIZE 0
/* Fake full size if we had a RO partition */
#undef CONFIG_RW_SIZE
#define CONFIG_RW_SIZE CONFIG_FLASH_SIZE
#endif /* HAS_TASK_CONSOLE */

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#undef  CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1

/* Optional features */
#define CONFIG_ADC
#undef  CONFIG_ADC_WATCHDOG
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_COMMON_GPIO_SHORTNAMES
#undef  CONFIG_DEBUG_ASSERT
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_HIBERNATE
#define CONFIG_HIBERNATE_WAKEUP_PINS STM32_PWR_CSR_EWUP2
#undef  CONFIG_HOSTCMD_EVENTS
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_I2C_SLAVE
#undef  CONFIG_LID_SWITCH
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LTO
#define CONFIG_STM_HWTIMER32
#undef  CONFIG_TASK_PROFILING
#undef  CONFIG_UART_TX_BUF_SIZE
#undef  CONFIG_UART_TX_DMA
#undef  CONFIG_UART_RX_DMA
#define CONFIG_UART_TX_BUF_SIZE 128
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_PORT_COUNT 2
#define CONFIG_USB_PD_TCPC
#define CONFIG_USB_PD_TCPM_VBUS
#define CONFIG_USBC_VCONN
#define CONFIG_VBOOT_HASH
#define CONFIG_WATCHDOG
#undef  CONFIG_WATCHDOG_HELP

/*
 * TODO(crosbug.com/p/50519): Remove CONFIG_SYSTEM_UNLOCKED prior to building
 * MP FW.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifdef HAS_TASK_CONSOLE
#undef  CONFIG_CONSOLE_HISTORY
#define CONFIG_CONSOLE_HISTORY 2

#else
#undef  CONFIG_CONSOLE_CMDHELP
#define CONFIG_DEBUG_PRINTF
#define UARTN CONFIG_UART_CONSOLE
#define UARTN_BASE STM32_USART_BASE(CONFIG_UART_CONSOLE)
#endif /* HAS_TASK_CONSOLE */

/* Use PSTATE embedded in the RO image, not in its own erase block */
#undef  CONFIG_FLASH_PSTATE_BANK
#undef  CONFIG_FW_PSTATE_SIZE
#define CONFIG_FW_PSTATE_SIZE 0

/* I2C ports configuration */
#define I2C_PORT_SLAVE  0
#define I2C_PORT_EC I2C_PORT_SLAVE

/* slave address for host commands */
#ifdef HAS_TASK_HOSTCMD
#define CONFIG_HOSTCMD_I2C_SLAVE_ADDR CONFIG_USB_PD_I2C_SLAVE_ADDR
#endif

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

#include "gpio_signal.h"

/* ADC signal */
enum adc_channel {
	ADC_C1_CC1_PD = 0,
	ADC_C0_CC1_PD,
	ADC_C0_CC2_PD,
	ADC_C1_CC2_PD,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* 1.5A Rp */
#define PD_SRC_VNC            PD_SRC_1_5_VNC_MV
#define PD_SRC_RD_THRESHOLD   PD_SRC_1_5_RD_THRESH_MV

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
