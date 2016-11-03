/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* oak board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* board revision */
#include "board_revs.h"

#if BOARD_REV >= OAK_REV5
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCEL_KX022
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#endif

#define CONFIG_ADC
#undef  CONFIG_ADC_WATCHDOG

#if BOARD_REV >= OAK_REV5
/* Add for Ambient Light Sensor */
#define CONFIG_ALS
#define CONFIG_ALS_OPT3001
#define CONFIG_CMD_ALS
#endif

/* Add for AC adaptor, charger, battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_BAT_PRESENT_L
#define CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD
#define CONFIG_BATTERY_SMART
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER

#define CONFIG_CHARGER_INPUT_CURRENT 512

#if BOARD_REV == OAK_REV1
#define CONFIG_CHARGER_BQ24773
#define CONFIG_CHARGER_MAX_INPUT_CURRENT 2150
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#else
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_CHARGER_ISL9237
#define CONFIG_CHARGER_MAX_INPUT_CURRENT 2250
#define CONFIG_CHARGER_NARROW_VDC
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20
#endif /* BOARD_REV */

#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_V2
#define CONFIG_CHIPSET_MEDIATEK
#define CONFIG_CMD_TYPEC
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_CMD_CHARGER_ADC_AMON_BMON

/*
 * EC_WAKE: PA0 - WKUP1
 * POWER_BUTTON_L: PB5 - WKUP6
 */
#define CONFIG_HIBERNATE
#if BOARD_REV <= OAK_REV4
#define CONFIG_HIBERNATE_WAKEUP_PINS (STM32_PWR_CSR_EWUP6)
#else
#define CONFIG_HIBERNATE_WAKEUP_PINS (STM32_PWR_CSR_EWUP1)
#endif /* BOARD_REV */
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_HOSTCMD_PD
#define CONFIG_HOSTCMD_PD_PANIC
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_LED_COMMON
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE 0
#define CONFIG_LID_ANGLE_SENSOR_LID 2
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_SWITCH
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_MKBP_EVENT
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_COMMON
#define CONFIG_USB_CHARGER
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_LOG_SIZE 512
#define CONFIG_USB_PD_PORT_COUNT 2
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_SPI
#define CONFIG_SPI_MASTER
#define CONFIG_STM_HWTIMER32
#define CONFIG_VBOOT_HASH
#undef  CONFIG_WATCHDOG_HELP
#define CONFIG_SWITCH
#define CONFIG_BOARD_VERSION
#undef  CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_TMP432
#define CONFIG_DPTF

/* UART DMA */
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* Optional features */
#define CONFIG_CMD_HOSTCMD

/* Drivers */
/* USB Mux */
#define CONFIG_USB_MUX_PI3USB30532
#if BOARD_REV >= OAK_REV5
#define CONFIG_USB_MUX_PS8740
#endif
/* BC 1.2 charger */
#define CONFIG_USB_SWITCH_PI3USB9281
#define CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT 2

#ifndef __ASSEMBLER__

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_A, GPIO_B, GPIO_C, GPIO_D

/* 2 I2C master ports, connect to battery, charger, pd and USB switches */
#define I2C_PORT_MASTER  0
#define I2C_PORT_ACCEL   0
#define I2C_PORT_ALS     0
#define I2C_PORT_BATTERY 0
#define I2C_PORT_CHARGER 0
#define I2C_PORT_PERICOM 0
#define I2C_PORT_THERMAL 0
#define I2C_PORT_PD_MCU  1
#define I2C_PORT_USB_MUX 1
#define I2C_PORT_TCPC    1

/* Enable Accel over SPI */
#define CONFIG_SPI_ACCEL_PORT    0  /* First SPI master port (SPI2) */

/* Ambient Light Sensor address */
#define OPT3001_I2C_ADDR OPT3001_I2C_ADDR1

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_WATCHDOG 4

/* Define the MKBP events which are allowed to wakeup AP in S3. */
#define CONFIG_MKBP_WAKEUP_MASK \
		(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEY_PRESSED) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEYBOARD_RECOVERY) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEYBOARD_FASTBOOT))

#include "gpio_signal.h"

enum power_signal {
	MTK_POWER_GOOD = 0,
	MTK_SUSPEND_ASSERTED,
	/* Number of power signals */
	POWER_SIGNAL_COUNT
};

enum pwm_channel {
	PWM_CH_POWER_LED = 0,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum adc_channel {
	ADC_PSYS = 0,  /* PC1: STM32_AIN(2) */
	ADC_AMON_BMON, /* PC0: STM32_AIN(10) */
	ADC_VBUS,      /* PA2: STM32_AIN(11) */
	ADC_CH_COUNT
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

	ALS_COUNT
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

/* delay to turn on/off vconn */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     CONFIG_CHARGER_MAX_INPUT_CURRENT
#define PD_MAX_VOLTAGE_MV     20000

/* Reset PD MCU */
void board_reset_pd_mcu(void);
/* Set AP reset pin according to parameter */
void board_set_ap_reset(int asserted);

/* Control type-C DP route and hotplug detect signal */
void board_typec_dp_on(int port);
void board_typec_dp_off(int port, int *dp_flags);
void board_typec_dp_set(int port, int level);

#endif  /* !__ASSEMBLER__ */

#endif  /* __CROS_EC_BOARD_H */
