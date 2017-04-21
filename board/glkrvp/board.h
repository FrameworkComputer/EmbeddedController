/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel GLK-RVP board-specific configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

#define CC_DEFAULT     (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
#undef CONFIG_HOSTCMD_DEBUG_MODE

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* EC console commands  */

/* Battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_SMART

/* Charger */
#define CONFIG_CHARGER
#define CONFIG_CHARGER_INPUT_CURRENT 2250
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGER_NARROW_VDC
#define CONFIG_CHARGER_PROFILE_OVERRIDE
#define CONFIG_CHARGER_PROFILE_OVERRIDE_COMMON
#undef  CONFIG_CHARGER_PROFILE_VOLTAGE_RANGES
#define CONFIG_CHARGER_PROFILE_VOLTAGE_RANGES 3
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_V2
#undef  CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 1000
#define CONFIG_EXTPOWER_GPIO

/* Keyboard */
#define CONFIG_KEYBOARD_PROTOCOL_8042

/* UART */
#define NPCX_UART_MODULE2    1 /* 0:GPIO10/11 1:GPIO64/65 as UART */

/* USB-A config */

/* USB PD config */

/* SoC / PCH */
#define CONFIG_LPC
#define CONFIG_CHIPSET_APOLLOLAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_SCI_GPIO GPIO_PCH_SCI_L

/* EC */
#define CONFIG_BOARD_VERSION
#define CONFIG_BOARD_SPECIFIC_VERSION
#define CONFIG_WP_ALWAYS
#define CONFIG_I2C
#define CONFIG_I2C_MASTER

#define CONFIG_LID_SWITCH
#define CONFIG_LTO
#define CONFIG_UART_HOST 0

#define CONFIG_FLASH_SIZE 524288
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q40

/*
 * Enable 1 slot of secure temporary storage to support
 * suspend/resume with read/write memory training.
 */
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Optional feature - used by nuvoton */
#define NPCX_JTAG_MODULE2    0 /* 0:GPIO21/17/16/20 1:GPIOD5/E2/D4/E5 as JTAG*/
#define NPCX_TACH_SEL2       0 /* 0:GPIO40/A4 1:GPIO93/D3 as TACH */

/* I2C ports */
#define I2C_PORT_CHARGER	NPCX_I2C_PORT3
#define I2C_PORT_BATTERY	NPCX_I2C_PORT3

/* EC exclude modules */
#undef CONFIG_ADC
#undef CONFIG_PECI
#undef CONFIG_WATCHDOG

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum power_signal {
	X86_RSMRST_N = 0,
	X86_SLP_S0_N,
	X86_SLP_S3_N,
	X86_SLP_S4_N,

	X86_ALL_SYS_PG,		/* PMIC_EC_PWROK_OD */

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum adc_channel {
	ADC_CH_COUNT,
};

int board_get_version(void);

/* Define typical operating power and max power */

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
