/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Skylake Chrome Reference Design board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_ADC
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_VERSION
#define CONFIG_BUTTON_COUNT 2
#define CONFIG_CHARGE_MANAGER

#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2

#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_ISL9237
#define CONFIG_CHARGER_ILIM_PIN_DISABLED
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT 1
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 15000
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_CHARGER_NARROW_VDC
#define CONFIG_CHARGER_PSYS
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20
#define CONFIG_TRICKLE_CHARGING

/* PSYS resistor 8.25KOhm */
#define CHARGER_PSYS_RESISTOR 8250

/*
 * PSYS gain = 1 / (PSYS resistor * (1.44 or 0.36) uA/W)
 */
#define ISL9237_C2_PSYS_GAIN_1_44 (100000000ul / (CHARGER_PSYS_RESISTOR * 144))
#define ISL9237_C2_PSYS_GAIN_0_36 (100000000ul / (CHARGER_PSYS_RESISTOR * 36))

#define CONFIG_CHIPSET_SKYLAKE
#define CONFIG_CLOCK_CRYSTAL
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_FANS 1
#define CONFIG_HOSTCMD_PD
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_COMMON
#define CONFIG_LID_SWITCH
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LTO
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
/* All data won't fit in data RAM.  So, moving boundary slightly. */
#undef CONFIG_RO_SIZE
#define CONFIG_RO_SIZE (104 * 1024)
#define CONFIG_SCI_GPIO GPIO_PCH_SCI_L
/* We're space constrained on Kunimitsu, so reduce the UART TX buffer size. */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 512
#define CONFIG_POWER_S0IX
#define CONFIG_USB_CHARGER
#define CONFIG_USB_MUX_PI3USB30532
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_PORT_COUNT 2
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_SWITCH_PI3USB9281
#define CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT 2
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN

/* USB-A ports */
#define USB_PORT_COUNT 2
#define CONFIG_USB_PORT_POWER_DUMB

#define CONFIG_VBOOT_HASH

#define CONFIG_SPI_FLASH_PORT 1
#define CONFIG_SPI_FLASH
#define CONFIG_FLASH_SIZE 524288
#define CONFIG_SPI_FLASH_W25X40

#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_TMP432

/*
 * Allow dangerous commands.
 * TODO(shawnn): Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED
#define CONFIG_WATCHDOG_HELP

/* LED signals */
#define GPIO_BAT_LED_AMBER GPIO_CHARGE_LED1
#define GPIO_BAT_LED_BLUE GPIO_CHARGE_LED2

/* I2C ports */
#define I2C_PORT_BATTERY MEC1322_I2C3
#define I2C_PORT_CHARGER MEC1322_I2C3
#define I2C_PORT_THERMAL MEC1322_I2C3
#define I2C_PORT_USB_CHARGER_1 MEC1322_I2C0_1
#define I2C_PORT_USB_MUX MEC1322_I2C0_1
#define I2C_PORT_PD_MCU MEC1322_I2C1
#define I2C_PORT_TCPC MEC1322_I2C1
#define I2C_PORT_ALS MEC1322_I2C2
#define I2C_PORT_ACCEL MEC1322_I2C2
#define I2C_PORT_GYRO MEC1322_I2C2
#define I2C_PORT_PMIC MEC1322_I2C0_0
#define I2C_PORT_USB_CHARGER_2 MEC1322_I2C0_0

#undef DEFERRABLE_MAX_COUNT
#define DEFERRABLE_MAX_COUNT 14

#define CONFIG_ALS
#define CONFIG_ALS_OPT3001
#define OPT3001_I2C_ADDR OPT3001_I2C_ADDR1

/* Accelerometer */
#ifdef HAS_TASK_MOTIONSENSE
#define CONFIG_ACCEL_KXCJ9
/* TODO: Enable support for gyrometer once space is availble. */
/* #define CONFIG_GYRO_L3GD20H */
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE 0
#define CONFIG_LID_ANGLE_SENSOR_LID 1
#endif

/* Modules we want to exclude */
#undef CONFIG_CMD_ACCEL_INFO
#undef CONFIG_CMD_ACCELS
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_TEMP_SENSOR
#undef CONFIG_CMD_TIMERINFO
#undef CONFIG_CONSOLE_CMDHELP
#undef CONFIG_CONSOLE_HISTORY
#undef CONFIG_PECI

/* Enable Pseudo G3 */
#define CONFIG_LOW_POWER_PSEUDO_G3

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* ADC signal */
enum adc_channel {
	ADC_BATT_PRESENT,
	ADC_VBUS,
	ADC_AMON_BMON,
	ADC_PSYS,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* power signal definitions */
enum power_signal {
	X86_RSMRST_L_PWRGD = 0,
	X86_SLP_S0_DEASSERTED,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,
	X86_SLP_SUS_DEASSERTED,
	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum temp_sensor_id {
	/* TMP432 local and remote sensors */
	TEMP_SENSOR_I2C_TMP432_LOCAL,
	TEMP_SENSOR_I2C_TMP432_REMOTE1,
	TEMP_SENSOR_I2C_TMP432_REMOTE2,

	/* Battery temperature sensor */
	TEMP_SENSOR_BATTERY,

	TEMP_SENSOR_COUNT
};

/* Light sensors */
enum als_id {
	ALS_OPT3001 = 0,

	ALS_COUNT,
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

/* Reset PD MCU */
void board_reset_pd_mcu(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
