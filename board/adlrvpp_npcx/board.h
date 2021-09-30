/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADL-P-RVP-NPCX board-specific configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* ITE EC variant */
#define VARIANT_INTELRVP_EC_NPCX

#include "adlrvp.h"

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
/* Power sequencing */
#define GPIO_EC_SPI_OE_N		GPIO_EC_SPI_OE_MECC
#define GPIO_PG_EC_ALL_SYS_PWRGD	GPIO_ALL_SYS_PWRGD
#define GPIO_PG_EC_RSMRST_ODL		GPIO_RSMRST_PWRGD
#define GPIO_PCH_SLP_S0_L		GPIO_PCH_SLP_S0_N
#define GPIO_PG_EC_DSW_PWROK		GPIO_VCCPDSW_3P3
#define GPIO_SLP_SUS_L			GPIO_PM_SLP_SUS_EC_N
#define GPIO_SYS_RESET_L		GPIO_SYS_RST_ODL
#define GPIO_PCH_RSMRST_L		GPIO_PM_RSMRST_N
#define GPIO_PCH_PWRBTN_L		GPIO_PM_PWRBTN_N
#define GPIO_EN_PP3300_A		GPIO_EC_DS3
#define GPIO_SYS_PWROK_EC		GPIO_SYS_PWROK
#define GPIO_PCH_DSW_PWROK		GPIO_EC_DSW_PWROK

/* Sensors */
#define GMR_TABLET_MODE_GPIO_L		GPIO_SLATE_MODE_INDICATION
#define GPIO_CPU_PROCHOT		GPIO_PROCHOT_EC_N

/* Buttons */
#define GPIO_LID_OPEN			GPIO_SMC_LID
#define GPIO_VOLUME_UP_L		GPIO_VOLUME_UP
#define GPIO_VOLUME_DOWN_L		GPIO_VOL_DN_EC
#define GPIO_POWER_BUTTON_L		GPIO_MECH_PWR_BTN_ODL

/* H1 */
#define GPIO_WP_L			GPIO_EC_FLASH_WP_ODL
#define GPIO_PACKET_MODE_EN		GPIO_EC_H1_PACKET_MODE
#define GPIO_ENTERING_RW		GPIO_EC_ENTERING_RW

/* AC & Battery */
#define GPIO_DC_JACK_PRESENT		GPIO_STD_ADP_PRSNT
#define GPIO_AC_PRESENT			GPIO_BC_ACOK
#define CONFIG_BATTERY_PRESENT_GPIO	GPIO_BAT_DET

/* eSPI/Host communication */
#define GPIO_ESPI_RESET_L		GPIO_LPC_ESPI_RST_N
#define GPIO_PCH_WAKE_L			GPIO_SMC_WAKE_SCI_N_MECC
#define GPIO_EC_INT_L			GPIO_EC_PCH_MKBP_INT_ODL

/* LED */
#define GPIO_BAT_LED_RED_L		GPIO_LED_1_L
#define GPIO_PWR_LED_WHITE_L		GPIO_LED_2_L

/* FAN */
#define GPIO_FAN_POWER_EN		GPIO_THERM_SEN_MECC

/* I2C ports & Configs */
/* Charger */
#define I2C_PORT_CHARGER	NPCX_I2C_PORT7_0

/* Battery */
#define I2C_PORT_BATTERY	NPCX_I2C_PORT7_0

/* Board ID */
#define I2C_PORT_PCA9555_BOARD_ID_GPIO	NPCX_I2C_PORT7_0

/* Port 80 */
#define I2C_PORT_PORT80		NPCX_I2C_PORT7_0

/* USB-C I2C */
#define I2C_PORT_TYPEC_0	NPCX_I2C_PORT0_0
/*
 * Note: I2C for Type-C Port-1 is swapped with Type-C Port-2
 *       on the RVP to reduce BOM stuffing options.
 */
#define I2C_PORT_TYPEC_1	NPCX_I2C_PORT2_0
#if defined(HAS_TASK_PD_C2)
#define I2C_PORT_TYPEC_2	NPCX_I2C_PORT1_0
#endif
#if defined(HAS_TASK_PD_C3)
#define I2C_PORT_TYPEC_3	NPCX_I2C_PORT3_0
#endif

#ifndef __ASSEMBLER__

enum adlrvp_i2c_channel {
	I2C_CHAN_BATT_CHG,
	I2C_CHAN_TYPEC_0,
	I2C_CHAN_TYPEC_1,
#if defined(HAS_TASK_PD_C2)
	I2C_CHAN_TYPEC_2,
#endif
#if defined(HAS_TASK_PD_C3)
	I2C_CHAN_TYPEC_3,
#endif
	I2C_CHAN_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
