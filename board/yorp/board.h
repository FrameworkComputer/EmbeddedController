/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Yorp board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

/* NPCX7 config */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
#define NPCX_TACH_SEL2    0  /* [0:GPIO40/73, 1:GPIO93/A6] as TACH */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */

/* Internal SPI flash on NPCX7 */
/* Flash is 1MB but reserve half for future use. */
#define CONFIG_FLASH_SIZE (512 * 1024)

#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q128 /* Internal SPI flash type. */

#define CONFIG_POWER_BUTTON

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* TODO(b/73811887): Fill out correctly */
enum adc_channel {
	ADC_CH_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
