/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Stellaris EKB-LM4F-EAC board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED  /* Allow dangerous commands */
#define CONFIG_I2C
#define CONFIG_I2C_MASTER

/* LM4 modules we don't use on link but still want to keep compiling */
#define CONFIG_EEPROM
#define CONFIG_PSTORE

/* Modules we want to exclude */
#undef CONFIG_LID_SWITCH
#undef CONFIG_HOSTCMD_LPC
#undef CONFIG_PECI
#undef CONFIG_SWITCH

/* Write protect is active high */
#define CONFIG_WP_ACTIVE_HIGH

#ifndef __ASSEMBLER__

enum adc_channel {
	ADC_CH_EC_TEMP = 0,  /* EC internal die temperature in degrees K. */
	ADC_CH_BDS_POT,      /* BDS pot input. */
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_COUNT
};

/* I2C ports */
#define I2C_PORT_LIGHTBAR 5  /* Port 5 / PA6:7 on link, but PG6:7 on badger */

/* Second UART port */
#define CONFIG_UART_HOST 1

#include "gpio_signal.h"

/* EEPROM blocks */
#define EEPROM_BLOCK_START_PSTORE 16  /* Host persistent storage */
#define EEPROM_BLOCK_COUNT_PSTORE 16

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
