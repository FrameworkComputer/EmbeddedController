/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Awasuki board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_DEDEDE_EC_IT8320
#define BOARD_AWASUKI
#include "baseboard.h"
#undef GPIO_VOLUME_UP_L
#undef GPIO_VOLUME_DOWN_L
#undef CONFIG_VOLUME_BUTTONS
#undef CONFIG_USB_CHARGER

/* EC console commands */
#define CONFIG_CMD_TCPC_DUMP
#define CONFIG_CMD_CHARGER_DUMP

/* Bring up function */
#define CONFIG_CMD_I2C_SCAN
#define CONFIG_CMD_I2C_XFER
#define CONFIG_SYSTEM_UNLOCKED

/* Battery */
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1
#define CONFIG_HOSTCMD_BATTERY_V2
#undef CONFIG_BATT_HOST_FULL_FACTOR
#define CONFIG_BATT_HOST_FULL_FACTOR 99
#undef CONFIG_I2C_NACK_RETRY_COUNT
#define CONFIG_I2C_NACK_RETRY_COUNT 10
#define CONFIG_SMART_BATTERY_OPTIONAL_MFG_FUNC
#define CONFIG_SMBUS_PEC

/* Charger */
#define CONFIG_CHARGER_RAA489000
#define PD_MAX_VOLTAGE_MV 20000
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_SENSE_RESISTOR 10
/*
 * b/147463641: The charger IC seems to overdraw ~4%, therefore we
 * reduce our target accordingly.
 */
#undef CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT
#define CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT 4
#define CONFIG_CHARGER_SINGLE_CHIP
#undef CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (100 * MSEC)
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 15000

/* LED */
#define CONFIG_LED_ONOFF_STATES
#define CONFIG_LED_ONOFF_STATES_BAT_LOW 10

/* PowerButton */
#undef CONFIG_POWER_BUTTON_INIT_TIMEOUT
#define CONFIG_POWER_BUTTON_INIT_TIMEOUT 2

/* TCPC */
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#define CONFIG_USB_PD_TCPM_RAA489000

/* USB defines specific to external TCPCs */
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_5V_EN_CUSTOM

/* Thermistors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_THROTTLE_AP
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* USB Mux */
#define CONFIG_USB_MUX_IT5205 /* ITE Mux */
#define I2C_PORT_USB_MUX I2C_PORT_USB_C0 /* Required for ITE Mux */

/* keyboard */
#define CONFIG_KEYBOARD_CUSTOMIZATION
#define CONFIG_KEYBOARD_REFRESH_ROW3
#define CONFIG_KEYBOARD_VIVALDI

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* ADC channels */
enum adc_channel {
	ADC_VSNS_PP3300_A, /* ADC0 */
	ADC_TEMP_SENSOR_1, /* ADC2 */
	ADC_TEMP_SENSOR_2, /* ADC3 */
	ADC_TEMP_SENSOR_3, /* ADC15*/
	ADC_TEMP_SENSOR_4, /* ADC13*/
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1,
	TEMP_SENSOR_2,
	TEMP_SENSOR_3,
	TEMP_SENSOR_4,
	TEMP_SENSOR_COUNT
};

/* List of possible batteries */
enum battery_type {
	BATTERY_C31N2314,
	BATTERY_C31N2315,
	BATTERY_TYPE_COUNT,
};

int als_enable_status(void);
#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
