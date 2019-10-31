/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

/*
 * Prevent the EC from powering up the AP by default for initial board bringup
 * TODO: b/142409811 remove this once AP power sequencing is good.
 */
#define CONFIG_BRINGUP

#define CONFIG_POWER_BUTTON

/* Config options automatically enabled by NPCX, re-enable once support added */
#undef CONFIG_ADC

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* Keyboard features */

/* Sensors */

/* USB Type C and USB PD defines */

/* USB Type A Features */

/* BC 1.2 */

/* Volume Button feature */

/* Fan features */

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_AC_PRESENT		GPIO_ACOK_OD
#define GPIO_EC_INT_L		EC_PCH_INT_ODL
#define GPIO_EN_PP5000		GPIO_EN_PP5000_A
#define GPIO_ENTERING_RW	GPIO_EC_ENTERING_RW
#define GPIO_LID_OPEN		GPIO_EC_LID_OPEN
#define GPIO_KBD_KSO2		GPIO_EC_KSO_02_INV
#define GPIO_PCH_WAKE_L		GPIO_EC_PCH_WAKE_ODL
#define GPIO_PCH_PWRBTN_L	GPIO_EC_PCH_PWR_BTN_ODL
#define GPIO_PCH_RSMRST_L	GPIO_EC_PCH_RSMRST_ODL
#define GPIO_PCH_RTCRST		GPIO_EC_PCH_RTCRST
#define GPIO_PCH_SYS_PWROK	GPIO_EC_PCH_SYS_PWROK
#define GPIO_PCH_SLP_S0_L	GPIO_SLP_S0_L
#define GPIO_PCH_SLP_S3_L	GPIO_SLP_S3_L
#define GPIO_PG_EC_DSW_PWROK	GPIO_DSW_PWROK
#define GPIO_POWER_BUTTON_L	GPIO_H1_EC_PWR_BTN_ODL
#define GPIO_RSMRST_L_PGOOD	GPIO_PG_EC_RSMRST_ODL
#define GPIO_CPU_PROCHOT	GPIO_EC_PROCHOT_ODL
#define GPIO_SYS_RESET_L	GPIO_SYS_RST_ODL
#define GPIO_WP_L		GPIO_EC_WP_L


#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* TODO: b/143375057 - Remove this code after power on. */
void c10_gate_change(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
