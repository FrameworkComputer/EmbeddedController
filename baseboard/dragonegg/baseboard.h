/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DragonEgg board configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* EC console commands  */
#define CONFIG_CMD_BATT_MFG_ACCESS

#define CONFIG_CHIPSET_ICELAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_HOSTCMD_ESPI
#define CONFIG_HOSTCMD_ESPI_VW_SLP_S3
#define CONFIG_HOSTCMD_ESPI_VW_SLP_S4
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_HOST_EVENT
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_PP5000_CONTROL
/* TODO(b/111155507): Don't enable SOiX for now */
/* #define CONFIG_POWER_S0IX */
/* #define CONFIG_POWER_TRACK_HOST_SLEEP_STATE */

/* EC Defines */
#define CONFIG_ADC
#define CONFIG_PWM
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* CBI */
/*
 * TODO (b/117174246): When EEPROMs are programmed, can use EEPROM for board
 * version. But for P0/P1 boards rely on GPIO signals.
 */
/* #define CONFIG_BOARD_VERSION_CBI */
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_CRC8

/* Common Keyboard Defines */
#define CONFIG_CMD_KEYBOARD
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI2

/* Common charger defines */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_CHARGER
#define CONFIG_CHARGER_BQ25710
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 512 /* Allow low-current USB charging */
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_CHARGER_NARROW_VDC
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10

/* Common battery defines */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_DEVICE_CHEMISTRY  "LION"
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_HW_PRESENT_CUSTOM
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_SMART

/* BC 1.2 Detection */
#define CONFIG_BC12_DETECT_MAX14637
#define CONFIG_USB_CHARGER

/* USB Type C and USB PD defines */
#undef CONFIG_USB_PD_TCPC_LOW_POWER
#undef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USB_PD_TCPM_ITE83XX	/* C0 & C1 TCPC: ITE EC */
#define CONFIG_USB_PD_TCPM_TUSB422	/* C1 TCPC: TUSB422 */
#define CONFIG_USB_POWER_DELIVERY
/*
 * TODO (b/111281797): DragonEgg has 3 ports. Only adding support for the port
 * on the MLB for now. In addition, this config option will likely move to
 * board.h as it likely board dependent and not same across all follower boards.
 */
#define CONFIG_USB_PD_PORT_MAX_COUNT 3
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT TYPEC_RP_3A0
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_PPC
/*
 * TODO(b/113541930): ADC measurements are available for port 0 and 1, but not
 * port 2.
 */
#define CONFIG_USB_PD_VBUS_MEASURE_NOT_PRESENT
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_MUX_VIRTUAL
#define CONFIG_USBC_PPC_SN5S330		/* C0 PPC */
#define CONFIG_USBC_PPC_SYV682X		/* C1 PPC */
#define CONFIG_USBC_PPC_NX20P3481		/* C2 PPC */
#define CONFIG_USBC_PPC_VCONN
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

#define CONFIG_CMD_PD_CONTROL
#define CONFIG_CMD_PPC_DUMP

/* TODO(b/111281797): Use correct PD delay values */
#define PD_POWER_SUPPLY_TURN_ON_DELAY	30000	/* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	250000	/* us */
#define PD_VCONN_SWAP_DELAY		5000	/* us */

/* TODO(b/111281797): Use correct PD power values */
#define PD_OPERATING_POWER_MW	15000
#define PD_MAX_POWER_MW		45000
#define PD_MAX_CURRENT_MA	3000
#define PD_MAX_VOLTAGE_MV	20000

/* I2C Bus Configuration */
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define I2C_PORT_BATTERY	IT83XX_I2C_CH_F	/* Shared bus */
#define I2C_PORT_CHARGER	IT83XX_I2C_CH_F	/* Shared bus */
#define I2C_PORT_SENSOR		IT83XX_I2C_CH_B
#define I2C_PORT_USBC0		IT83XX_I2C_CH_E
#define I2C_PORT_USBC1C2	IT83XX_I2C_CH_C
#define I2C_PORT_EEPROM		IT83XX_I2C_CH_A
#define I2C_ADDR_EEPROM_FLAGS	0x50

#ifndef __ASSEMBLER__

/* Forward declare common (within DragonEgg) board-specific functions */
void board_reset_pd_mcu(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
