/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Bip board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

/* ITE Config */
#define CONFIG_IT83XX_FLASH_CLOCK_48MHZ /* Flash clock must be > (50Mhz / 2) */

/* EC Features */
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1
#define CONFIG_CRC8
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_BOARD_VERSION_CBI

/* Charger Configuration */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
 /* TODO(b/76429930): Use correct driver below after writing BQ25703 driver */
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_INPUT_CURRENT 512 /* Allow low-current USB charging */
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
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
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_COMM_LOCKED
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_ITE83XX	/* C0 & C1 TCPC: ITE EC */
#define CONFIG_USB_MUX_IT5205		/* C0 MUX: IT5205 */
#define CONFIG_USB_PD_TCPM_PS8751	/* C1 Mux: PS8751 */
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_TCPCI_MUX_ONLY
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_PPC_SN5S330		/* C0 & C1 PPC: each SN5S330 */
#define CONFIG_USBC_PPC_VCONN
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
#define CONFIG_BC12_DETECT_BQ24392

/* USB-A Configuration */
#define CONFIG_USB_PORT_POWER_DUMB
#define USB_PORT_COUNT 2

/* TODO(b/76218141): Use correct PD delay values */
#define PD_POWER_SUPPLY_TURN_ON_DELAY	30000	/* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	250000	/* us */
#define PD_VCONN_SWAP_DELAY		5000	/* us */

/* TODO(b/76218141): Use correct PD power values */
#define PD_OPERATING_POWER_MW	15000
#define PD_MAX_POWER_MW		45000
#define PD_MAX_CURRENT_MA	3000
#define PD_MAX_VOLTAGE_MV	20000

/* I2C Bus Configuration */
#define I2C_PORT_BATTERY	IT83XX_I2C_CH_A	/* Shared bus */
#define I2C_PORT_CHARGER	IT83XX_I2C_CH_A	/* Shared bus */
#define I2C_PORT_SENSOR		IT83XX_I2C_CH_B
#define I2C_PORT_USBC0		IT83XX_I2C_CH_C
#define I2C_PORT_USBC1		IT83XX_I2C_CH_E
#define I2C_PORT_USB_MUX	I2C_PORT_USBC0	/* For MUX driver */
#define I2C_PORT_EEPROM		IT83XX_I2C_CH_F
#define I2C_ADDR_EEPROM		0xA0

/* SoC / PCH Configuration */
#define CONFIG_CHIPSET_GEMINILAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_ESPI
/* TODO(b/76023457): Enable Virtual Wires after bringup */
#define CONFIG_LPC
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_EXTPOWER_GPIO
/* TODO(b/75974377), increase CONFIG_EXTPOWER_DEBOUNCE_MS from 30 to 1000? */


#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_VBUS_C0,
	ADC_VBUS_C1,
	ADC_CH_COUNT
};

/* TODO(b/75972988): Fill out correctly */
enum pwm_channel {
	PWM_CH_COUNT
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

/* Forward declare board-specific functions */
void board_reset_pd_mcu(void);
void board_pd_vconn_ctrl(int port, int cc_pin, int enabled);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
