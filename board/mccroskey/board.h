/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* McCroskey board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Debug features */
/* TODO(crosbug.com/p/23494): turn off extra I2C debugging when it works */
#define CONFIG_I2C_DEBUG
#undef  CONFIG_TASK_PROFILING

/* Features not present on this reference board */
#undef CONFIG_LID_SWITCH

/* Optional features */
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_KEYBOARD_PROTOCOL_MKBP

/*
 * TODO(crosbug.com/p/23494): Stop mode causes the UART to drop characters and
 * likely other bad side-effects. Disable for now.
 */
#undef   CONFIG_LOW_POWER_IDLE

#ifndef __ASSEMBLER__

/* Keyboard output ports */
#define KB_OUT_PORT_LIST GPIO_C

/* EC is I2C master */
#define I2C_PORT_MASTER 0
#define I2C_PORT_SLAVE 0	/* needed for DMAC macros (ugh) */
#define GPIO_I2C2_SCL 0		/* unused, but must be defined anyway */
#define GPIO_I2C2_SDA 0		/* unused, but must be defined anyway */

/* Timer selection */
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 4
#define TIM_WATCHDOG  1

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
