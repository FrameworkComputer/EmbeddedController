/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MEC1322 eval board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED  /* Allow dangerous commands */
#define CONFIG_WATCHDOG_HELP

/* Modules we want to exclude */
#undef CONFIG_EEPROM
#undef CONFIG_EOPTION
#undef CONFIG_PSTORE
#undef CONFIG_LID_SWITCH
#undef CONFIG_PECI
#undef CONFIG_SWITCH

#ifndef __ASSEMBLER__

/* GPIO signal list */
enum gpio_signal {
	GPIO_LED1 = 0,
	GPIO_LED2,
	GPIO_LED3,
	/*
	 * Signals which aren't implemented on MEC1322 eval board but we'll
	 * emulate anyway, to make it more convenient to debug other code.
	 */
	GPIO_RECOVERYn,           /* Recovery signal from DOWN button */
	GPIO_WP,                  /* Write protect input */
	GPIO_ENTERING_RW,         /* EC entering RW code */

	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
