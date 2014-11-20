/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ryu sensor board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART1 (PA9/PA10) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1

/*
 * The firmware image size is limited to 40K to
 * leave more space to the RW image.
 */
#undef CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_IMAGE_SIZE (40*1024)

/* By default, enable all console messages  */
#define CC_DEFAULT     CC_ALL

/* Optional features */
#undef CONFIG_EXTPOWER
#undef CONFIG_HIBERNATE
#define CONFIG_STM_HWTIMER32
#define CONFIG_I2C
#define CONFIG_BOARD_PRE_INIT
#undef  CONFIG_LID_SWITCH
#undef CONFIG_CMD_POWER_AP
#define CONFIG_POWER_COMMON
#define CONFIG_VBOOT_HASH
#undef CONFIG_WATCHDOG_HELP

/* I2C ports configuration */
#define I2C_PORT_SLAVE  0
#define I2C_PORT_EC I2C_PORT_SLAVE

/* slave address for host commands */
#ifdef HAS_TASK_HOSTCMD
#define CONFIG_HOSTCMD_I2C_SLAVE_ADDR 0x3e
#endif

/*
 * Write protect is active high, but given WP line is not implemented,
 * the memory is not write protected.
 */
#define CONFIG_WP_ACTIVE_HIGH

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
