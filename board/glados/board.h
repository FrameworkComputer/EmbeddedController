/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Glados board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_BATTERY_SMART
#define CONFIG_BUTTON_COUNT 2

#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_BQ24773
#define CONFIG_CHARGER_ILIM_PIN_DISABLED
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_INPUT_CURRENT 2240
#define CONFIG_CHARGER_DISCHARGE_ON_AC

#define CONFIG_CHIPSET_SKYLAKE
#define CONFIG_CLOCK_CRYSTAL
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_I2C
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_COMMON
#define CONFIG_LID_SWITCH
#define CONFIG_PORT80_TASK_EN
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_PORT_COUNT 1
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_SCI_GPIO GPIO_PCH_SCI_L

#define CONFIG_SPI_PORT 1
#define CONFIG_SPI_CS_GPIO GPIO_PVT_CS0
#define CONFIG_SPI_FLASH
#define CONFIG_SPI_FLASH_SIZE 524288
#define CONFIG_SPI_FLASH_W25Q64

/*
 * Allow dangerous commands.
 * TODO(shawnn): Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED
#define CONFIG_WATCHDOG_HELP

/* I2C ports */
#define I2C_PORT_BATTERY MEC1322_I2C0_0
#define I2C_PORT_CHARGER MEC1322_I2C0_0
#define I2C_PORT_PD_MCU MEC1322_I2C1
#define I2C_PORT_TCPC MEC1322_I2C1
#define I2C_PORT_ALS MEC1322_I2C2
#define I2C_PORT_ACCEL MEC1322_I2C2
#define I2C_PORT_PMIC MEC1322_I2C3

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* power signal definitions */
enum power_signal {
	X86_RSMRST_L_PWRGD = 0,
	X86_SLP_S0_DEASSERTED,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,
	X86_SLP_SUS_DEASSERTED,
	X86_PMIC_DPWROK,

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

/* start as a sink in case we have no other power supply/battery */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/* TODO: determine the following board specific type-C power constants */
/*
 * delay to turn on the power supply max is ~16ms.
 * delay to turn off the power supply max is about ~180ms.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

/* Discharge battery when on AC power for factory test. */
int board_discharge_on_ac(int enable);

/* Reset PD MCU */
void board_reset_pd_mcu(void);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
