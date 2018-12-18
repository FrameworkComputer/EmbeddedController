/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hatch board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

#define CONFIG_POWER_BUTTON
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_COMMON
#define CONFIG_LOW_POWER_IDLE

#define CONFIG_HOSTCMD_ESPI
/* #define CONFIG_HOSTCMD_ESPI_VW_SIGNALS */

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_PCH_RSMRST_L GPIO_EC_PCH_RSMRST_L
#define GPIO_PCH_SLP_S0_L GPIO_SLP_S0_L
#define GPIO_CPU_PROCHOT GPIO_EC_PROCHOT_ODL
#define GPIO_AC_PRESENT GPIO_ACOK_OD
#define GPIO_RSMRST_L_PGOOD GPIO_PG_EC_RSMRST_L
#define GPIO_PCH_SLP_S3_L GPIO_SLP_S3_L
#define GPIO_PCH_SLP_S4_L GPIO_SLP_S4_L

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_COUNT
};

/* List of possible batteries */
enum battery_type {
	BATTERY_KEYSTONE,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
