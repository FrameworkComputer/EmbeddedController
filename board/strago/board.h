/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Strago board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED  /* Allow dangerous commands */
#undef CONFIG_WATCHDOG_HELP
#define CONFIG_CLOCK_CRYSTAL
#define CONFIG_CHIPSET_BRASWELL
#define CONFIG_SCI_GPIO GPIO_PCH_SCI_L

#define CONFIG_BOARD_VERSION_GPIO

#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_IRQ_GPIO GPIO_KBD_IRQ_L
#undef CONFIG_KEYBOARD_KSO_BASE
#define CONFIG_KEYBOARD_KSO_BASE 4 /* KSO starts from KSO04 */
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_LID_SWITCH
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SHUTDOWN_PAUSE_IN_S5
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_LTO

/* All data won't fit in data RAM.  So, moving boundary slightly. */
#undef CONFIG_RO_SIZE
#define CONFIG_RO_SIZE (104 * 1024)

#define CONFIG_USB_CHARGER
#define CONFIG_USB_MUX_PI3USB30532
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_GPIO
#define CONFIG_BC12_DETECT_PI3USB9281
#define CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT 1
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN

#define CONFIG_SPI_FLASH_PORT 1
#define CONFIG_SPI_FLASH
#define CONFIG_FLASH_SIZE 524288
#define CONFIG_SPI_FLASH_W25Q64

#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_USB_PORT_POWER_SMART_SIMPLE
#define GPIO_USB1_ILIM_SEL GPIO_USB_ILIM_SEL

#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_TMP432
#define CONFIG_DPTF

#define CONFIG_PMIC

#define CONFIG_ALS_ISL29035
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_SMART
#define CONFIG_CHARGER
#define CONFIG_CHARGER_BQ24770
#define CONFIG_CHARGER_ILIM_PIN_DISABLED
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_INPUT_CURRENT 2240
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGE_MANAGER
#define CONFIG_HOSTCMD_PD

#define CONFIG_PWM
#define CONFIG_LED_COMMON

#define CONFIG_I2C
#define CONFIG_I2C_MASTER

/* Accelerometer */
#define CONFIG_ACCEL_KXCJ9
#undef CONFIG_CMD_ACCELS
#undef CONFIG_CMD_ACCEL_INFO
#undef CONFIG_CMD_ACCELSPOOF
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

/* Number of buttons */
#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_DOWN_L GPIO_VOLUME_DOWN
#define GPIO_VOLUME_UP_L GPIO_VOLUME_UP

#define CONFIG_ADC

/* Modules we want to exclude */
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_HOSTCMD
#undef CONFIG_CMD_I2C_SCAN
#undef CONFIG_CMD_IDLE_STATS
#undef CONFIG_CMD_PD
#undef CONFIG_CMD_SHMEM
#undef CONFIG_CMD_TEMP_SENSOR
#undef CONFIG_CMD_TIMERINFO
#undef CONFIG_CONSOLE_CMDHELP
#undef CONFIG_CONSOLE_HISTORY
#undef CONFIG_EEPROM
#undef CONFIG_FANS
#undef CONFIG_PSTORE
#undef CONFIG_PECI
#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* I2C ports */
#define I2C_PORT_BATTERY	MEC1322_I2C0_0
#define I2C_PORT_CHARGER	MEC1322_I2C0_0
#define I2C_PORT_ACCEL		MEC1322_I2C1
#define I2C_PORT_GYRO		MEC1322_I2C1
#define I2C_PORT_ALS		MEC1322_I2C1
#define I2C_PORT_USB_CHARGER_1	MEC1322_I2C2
#define I2C_PORT_PD_MCU		MEC1322_I2C2
#define I2C_PORT_TCPC		MEC1322_I2C2
#define I2C_PORT_THERMAL	MEC1322_I2C3
#define I2C_PORT_USB_MUX	MEC1322_I2C2

/* ADC signal */
enum adc_channel {
	ADC_VBUS,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* power signal definitions */
enum power_signal {
	X86_ALL_SYS_PWRGD = 0,
	X86_RSMRST_L_PWRGD,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum pwm_channel {
	PWM_CH_LED_RED,
	PWM_CH_LED_BLUE,
	PWM_CH_LED_GREEN,
	/* Number of PWM channels */
	PWM_CH_COUNT
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

enum sensor_id {
	BASE_ACCEL,
	LID_ACCEL,
	SENSOR_COUNT,
};

/* Light sensors */
enum als_id {
	ALS_ISL29035 = 0,

	ALS_COUNT,
};

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
