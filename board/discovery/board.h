/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32L-discovery board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

/* Enable USART2 */
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART2
#define CONFIG_CMD_USART_INFO

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection
 *
 * "discovery" is an STM32L152RC, which is a "Category 3" product that
 * has the 32-bit timer TIM5. See Section 17 "General-purpose Timers" and
 * "Table 3. STM32L15xxx product categories":
 * https://www.st.com/resource/en/reference_manual/cd00240193-stm32l100xx-stm32l151xx-stm32l152xx-and-stm32l162xx-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf
 */
#define TIM_CLOCK32 5

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
