/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MEC1322 eval board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED  /* Allow dangerous commands */
#define CONFIG_WATCHDOG_HELP
#define CONFIG_FANS 1
#define CONFIG_ADC
#define CONFIG_SPI_FLASH_SIZE 0x00800000
#define CONFIG_SPI_FLASH_W25Q64
#define CONFIG_SPI_PORT 0
#define CONFIG_SPI_CS_GPIO GPIO_SHD_CS0

/* Modules we want to exclude */
#undef CONFIG_EEPROM
#undef CONFIG_EOPTION
#undef CONFIG_PSTORE
#undef CONFIG_LID_SWITCH
#undef CONFIG_PECI
#undef CONFIG_SWITCH

#ifndef __ASSEMBLER__

enum adc_channel {
	ADC_CH_1 = 0,
	ADC_CH_2,
	ADC_CH_3,
	ADC_CH_4,

	ADC_CH_COUNT
};

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
