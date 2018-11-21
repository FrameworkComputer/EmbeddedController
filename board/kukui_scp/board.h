/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kukui SCP configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_FLASH_SIZE 0x40000 /* Image file size: 256KB */
#undef  CONFIG_LID_SWITCH
#undef  CONFIG_FW_INCLUDE_RO

/* IPI configs */
#define CONFIG_IPI

/* IPI ID should be in sync across kernel and EC. */
#define IPI_SCP_INIT 0
#define IPI_HOST_COMMAND 1
#define IPI_MDP 2
#define IPI_VDEC_H264 3
#define IPI_VDEC_VP8 4
#define IPI_VDEC_VP9 5
#define IPI_VENC_H264 6
#define IPI_VENC_VP8 7
#define IPI_COUNT 8

#undef  CONFIG_UART_CONSOLE
/*
 * CONFIG_UART_CONSOLE
 *   0 - SCP UART0
 *   1 - SCP UART1
 *   2 - share with AP UART0
 */
#define CONFIG_UART_CONSOLE 0
#define   UART0_PINMUX_11_12
#undef    UART0_PINMUX_110_112

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED
/* Debugging features */
#define CONFIG_DEBUG_EXCEPTIONS
#define CONFIG_DEBUG_STACK_OVERFLOW
#define CONFIG_CMD_GPIO_EXTENDED

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
