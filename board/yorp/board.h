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

/* EC Features */
#define CONFIG_I2C
#define CONFIG_I2C_MASTER

/* Charger Configuration */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_INPUT_CURRENT 512 /* Allow low-current USB charging */
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_USB_CHARGER

/* Battery Configuration */
#define CONFIG_BATTERY_CUT_OFF
/* TODO(b/74427009): Ensure this works in dead battery conditions */
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_EC_BATT_PRES_L
#define CONFIG_BATTERY_SMART

/* USB-C Configuration */
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_PORT_COUNT 2
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT TYPEC_RP_3A0
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_COMM_LOCKED
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_ANX74XX	/* C0 TCPC: ANX7447QN */
#define CONFIG_USB_PD_TCPM_PS8751	/* C1 TCPC: PS8751 */
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_PPC /* TODO(b/74206647): Remove this one have real driver */
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_CMD_PD_CONTROL
#define CONFIG_BC12_DETECT_BQ24392
#define CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT

/* TODO(b/74388692): Adding USB-A BC 1.2 charging support */

/* TODO(b/74244817): Use correct PD delay values */
#define PD_POWER_SUPPLY_TURN_ON_DELAY	30000	/* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	250000	/* us */
#define PD_VCONN_SWAP_DELAY		5000	/* us */

/* TODO(b/74244817): Use correct PD power values */
#define PD_OPERATING_POWER_MW	15000
#define PD_MAX_POWER_MW		45000
#define PD_MAX_CURRENT_MA	3000
#define PD_MAX_VOLTAGE_MV	20000

/* I2C Bus Configuration */
#define I2C_PORT_BATTERY	NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC0		NPCX_I2C_PORT1_0
#define I2C_PORT_TCPC1		NPCX_I2C_PORT2_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT3_0
#define I2C_PORT_CHARGER	NPCX_I2C_PORT4_1
#define I2C_PORT_SENSOR		NPCX_I2C_PORT7_0

/* SoC / PCH */
/* GEMINILAKE reuses apollo lake power seq */
#define CONFIG_CHIPSET_APOLLOLAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_ESPI
/* TODO(b/74123961): Enable Virtual Wires after bringup */
#define CONFIG_LPC
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_EXTPOWER_GPIO
/* TODO(b/73811887), increase CONFIG_EXTPOWER_DEBOUNCE_MS from 30 to 1000? */


#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* TODO(b/73811887): Fill out correctly */
enum adc_channel {
	ADC_VBUS_C0,
	ADC_VBUS_C1,
	ADC_CH_COUNT
};

enum power_signal {
#ifdef CONFIG_POWER_S0IX
	X86_SLP_S0_N,		/* PCH  -> SLP_S0_L */
#endif
	X86_SLP_S3_N,		/* PCH  -> SLP_S3_L */
	X86_SLP_S4_N,		/* PCH  -> SLP_S4_L */
	X86_SUSPWRDNACK,	/* PCH  -> SUSPWRDNACK */

	X86_ALL_SYS_PG,		/* PMIC -> PMIC_EC_PWROK_OD */
	X86_RSMRST_N,		/* PMIC -> PMIC_EC_RSMRST_ODL */
	X86_PGOOD_PP3300,	/* PMIC -> PP3300_PG_OD */
	X86_PGOOD_PP5000,	/* PMIC -> PP5000_PG_OD */

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

void board_reset_pd_mcu(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
