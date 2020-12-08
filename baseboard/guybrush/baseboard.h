/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* NPCX9 config */
#define NPCX9_PWM1_SEL    1  /* GPIO C2 is used as PWM1. */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* Config options automatically enabled, re-enable once support added */
#undef CONFIG_ADC
#undef CONFIG_SWITCH
#undef CONFIG_SPI_FLASH
#undef CONFIG_SUPPORT_CHIP_HIBERNATION

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_LID_OPEN		GPIO_EC_LID_OPEN
#define GPIO_WP_L		GPIO_EC_WP_L

/* EC Defines */

/* Host communication */

/* Chipset config */

/* Common Keyboard Defines */

/* Sensors */

/* Common charger defines */

/* Common battery defines */

/* USB Type C and USB PD defines */

/* USB Type A Features */

/* BC 1.2 */

/* I2C Bus Configuration */

/* Keyboard features */

/* Volume Button feature */

/* Fan features */

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
