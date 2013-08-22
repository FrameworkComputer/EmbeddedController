/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32L-discovery board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#undef  CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Module IDs */
enum module_id {
	MODULE_UART,
	MODULE_CHIPSET,
};

/* By default, enable all console messages except keyboard */
#define CC_DEFAULT	(CC_ALL & ~CC_MASK(CC_KEYSCAN))

/* Timer selection */
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 4

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_USER_BUTTON = 0,
	/* Outputs */
	GPIO_LED_BLUE,
	GPIO_LED_GREEN,
	/* Unimplemented signals we emulate */
	GPIO_ENTERING_RW,
	GPIO_WP_L,
	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
