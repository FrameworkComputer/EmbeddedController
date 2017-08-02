/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Zoombini board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands. */

/* NPCX7 config */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
#define NPCX_TACH_SEL2    0  /* No tach. */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */

/* Internal SPI flash on NPCX7 */
#define CONFIG_FLASH_SIZE (512 * 1024) /* It's really 1MB. */
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* EC Modules */
#define CONFIG_ADC
#define CONFIG_ESPI
#define CONFIG_ESPI_VW_SIGNALS
#define CONFIG_I2C
#define CONFIG_LPC
#define CONFIG_PWM

#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_BAT_PRESENT_L

#define CONFIG_BOARD_VERSION

#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20
/* TODO(aaboagye): add when BC 1.2 stuff ready. */
#if 0
#define CONFIG_USB_CHARGER
#endif /* 0 */

#define CONFIG_CHIPSET_CANNONLAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_UART_HOST 0

#define CONFIG_I2C_MASTER

#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_SWITCH

#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_PORT_COUNT 3
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_LOGGING
/* TODO(aaboagye): What about CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT? */
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_TCPM_PS8805
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* Define typical operating power and max power. */
#define PD_MAX_VOLTAGE_MV 20000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_POWER_MW 45000
#define PD_OPERATING_POWER_MW 15000
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/* TODO(aaboagye): Verify these timings... */
/*
 * delay to turn on the power supply max is ~16ms.
 * delay to turn off the power supply max is about ~180ms.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY	30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	250000 /* us */

#define CONFIG_WIRELESS
#define WIRELESS_GPIO_WLAN_POWER GPIO_EN_PP3300_WLAN
#define WIRELESS_GPIO_WWAN_POWER GPIO_EN_PP3300_WWAN

/* I2C Ports */
#define I2C_PORT_BATTERY I2C_PORT_POWER
#define I2C_PORT_CHARGER I2C_PORT_POWER
#define I2C_PORT_POWER   NPCX_I2C_PORT0_0
#define I2C_PORT_PMIC    NPCX_I2C_PORT3_0
#define I2C_PORT_TCPC0   NPCX_I2C_PORT5_0
#define I2C_PORT_TCPC1   NPCX_I2C_PORT1_0
#define I2C_PORT_TCPC2   NPCX_I2C_PORT2_0

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* ADC signal */
enum adc_channel {
	ADC_VBUS = 0,
	ADC_TEMP_SENSOR_SOC,
	ADC_TEMP_SENSOR_CHARGER,
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_LED_GREEN = 0,
	PWM_CH_LED_RED,
	PWM_CH_KB_BL,
	PWM_CH_COUNT
};

enum power_signal {
	X86_SLP_S0_DEASSERTED,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,
	X86_SLP_SUS_DEASSERTED,
	X86_RSMRST_L_PGOOD,
	X86_PMIC_DPWROK,
	POWER_SIGNAL_COUNT
};

/* Reset all TCPCs. */
void board_reset_pd_mcu(void);

#endif /* !defined(__ASSEMBLER__) */

#endif /* __CROS_EC_BOARD_H */
