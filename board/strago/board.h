/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Strago board configuration */

#ifndef __BOARD_H
#define __BOARD_H


/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED  /* Allow dangerous commands */
#define CONFIG_WATCHDOG_HELP
#define CONFIG_CHIPSET_BRASWELL
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_KEYBOARD_IRQ_GPIO GPIO_KBD_IRQ_L
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_LID_SWITCH
#define CONFIG_POWER_COMMON
#define CONFIG_EXTPOWER_GPIO

/* Modules we want to exclude */
#undef CONFIG_EEPROM
#undef CONFIG_EOPTION
#undef CONFIG_PSTORE
#undef CONFIG_PECI
#undef CONFIG_SWITCH
#undef CONFIG_I2C
#undef CONFIG_PWM
#undef CONFIG_FANS
#undef CONFIG_ADC
#undef CONFIG_WAKE_PIN
#undef CONFIG_SPI
#undef CONFIG_SPI_PORT
#undef CONFIG_SPI_CS_GPIO

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

/* power signal definitions */
enum power_signal {
	X86_ALL_SYS_PWRGD = 0,
	X86_RSMRST_L_PWRGD,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
