/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADL-P-RVP-ITE board-specific configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* ITE EC variant */
#define VARIANT_INTELRVP_EC_IT8320

#include "adlrvp.h"

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_AC_PRESENT			GPIO_BC_ACOK_EC
#define GPIO_EC_INT_L			GPIO_EC_PCH_MKBP_INT_ODL_EC
#define GPIO_ENTERING_RW		GPIO_EC_ENTERING_RW_EC
#define GPIO_LID_OPEN			GPIO_SMC_LID
#define GPIO_PACKET_MODE_EN		GPIO_EC_H1_PACKET_MODE_EC
#define GPIO_PCH_WAKE_L			GPIO_PCH_WAKE_N
#define GPIO_PCH_PWRBTN_L		GPIO_PM_PWRBTN_N_EC
#define GPIO_PCH_RSMRST_L		GPIO_PM_RSMRST_EC
#define GPIO_PCH_SLP_S0_L		GPIO_PCH_SLP_S0_N
#define GPIO_PCH_SLP_S3_L		GPIO_SLP_S3_R_L
#define GPIO_PG_EC_DSW_PWROK		GPIO_VCCPDSW_3P3_EC
#define GPIO_POWER_BUTTON_L		GPIO_MECH_PWR_BTN_ODL
#define GPIO_CPU_PROCHOT		GPIO_PROCHOT_EC
#define GPIO_SYS_RESET_L		GPIO_SYS_RST_ODL_EC
#define GPIO_WP_L			GPIO_EC_WP_ODL
#define GPIO_VOLUME_UP_L		GPIO_VOLUME_UP
#define GPIO_VOLUME_DOWN_L		GPIO_VOL_DN_EC_R
#define GPIO_DC_JACK_PRESENT		GPIO_STD_ADP_PRSNT
#define GPIO_ESPI_RESET_L		GPIO_ESPI_RST_R
#define GPIO_UART1_RX			GPIO_UART_SERVO_TX_EC_RX
#define CONFIG_BATTERY_PRESENT_GPIO	GPIO_BAT_DET_EC
#define GPIO_BAT_LED_RED_L		GPIO_LED_1_L_EC
#define GPIO_PWR_LED_WHITE_L		GPIO_LED_2_L_EC
#define GPIO_SLP_SUS_L			GPIO_PM_SLP_SUS_EC
#define GPIO_PG_EC_RSMRST_ODL		GPIO_RSMRST_PWRGD_EC
#define GPIO_PG_EC_ALL_SYS_PWRGD	GPIO_ALL_SYS_PWRGD_EC
#define GPIO_PCH_DSW_PWROK		GPIO_DSW_PWROK_EC
#define GPIO_EN_PP3300_A		GPIO_EC_DS3
#define GMR_TABLET_MODE_GPIO_L		GPIO_SLATE_MODE_INDICATION

/* I2C ports & Configs */
#define CONFIG_IT83XX_SMCLK2_ON_GPC7

#define I2C_PORT_CHARGER	IT83XX_I2C_CH_B

/* Battery */
#define I2C_PORT_BATTERY	IT83XX_I2C_CH_B

/* Board ID */
#define I2C_PORT_PCA9555_BOARD_ID_GPIO	IT83XX_I2C_CH_B

/* Port 80 */
#define I2C_PORT_PORT80		IT83XX_I2C_CH_B

/* USB-C I2C */
#define I2C_PORT_TYPEC_0		IT83XX_I2C_CH_C
#define I2C_PORT_TYPEC_1		IT83XX_I2C_CH_F
#if defined(HAS_TASK_PD_C2)
#define I2C_PORT_TYPEC_2		IT83XX_I2C_CH_E
#define I2C_PORT_TYPEC_3		IT83XX_I2C_CH_D
#endif

/* TCPC */
#define CONFIG_USB_PD_TCPM_ITE_ON_CHIP
#define CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT 1

/* Config Fan */
#define GPIO_FAN_POWER_EN	GPIO_EC_THRM_SEN_PWRGATE_N
#define GPIO_ALL_SYS_PWRGD	GPIO_ALL_SYS_PWRGD_EC

/* Increase EC speed */
#undef PLL_CLOCK
#define PLL_CLOCK	96000000

#ifndef __ASSEMBLER__

enum adlrvp_i2c_channel {
	I2C_CHAN_FLASH,
	I2C_CHAN_BATT_CHG,
	I2C_CHAN_TYPEC_0,
	I2C_CHAN_TYPEC_1,
#if defined(HAS_TASK_PD_C2)
	I2C_CHAN_TYPEC_2,
	I2C_CHAN_TYPEC_3,
#endif
	I2C_CHAN_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
