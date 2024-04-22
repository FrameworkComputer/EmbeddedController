/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledee board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_DEDEDE_EC_IT8320
#include "baseboard.h"

/* System unlocked in early development */
#undef CONFIG_SYSTEM_UNLOCKED

#define CONFIG_CMD_TCPC_DUMP
#define CONFIG_CMD_CHARGER_DUMP

/* Battery */
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_V2
#define CONFIG_HOSTCMD_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1

/* BC 1.2 */
#define CONFIG_BC12_DETECT_PI3USB9201

/* Charger */
#define CONFIG_CHARGER_RAA489000 /* C0 and C1: Charger */
#define PD_MAX_VOLTAGE_MV 20000
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#undef CONFIG_CHARGER_SINGLE_CHIP
#define CONFIG_OCPC
#define CONFIG_OCPC_DEF_RBATT_MOHMS               \
	22 /* R_DS(on) 11.6mOhm + 10mOhm sns rstr \
	    */
#undef CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (100 * MSEC)

#define GPIO_USB_C1_INT_ODL GPIO_SUB_USB_C1_INT_ODL

/* LED */
#define CONFIG_LED_ONOFF_STATES
#define CONFIG_LED_ONOFF_STATES_BAT_LOW 10

/* PWM */
#define CONFIG_PWM

/* Sensors */
#define CONFIG_ACCEL_KX022 /* Lid accel */
#define CONFIG_ACCELGYRO_LSM6DSM /* Base accel */
#define CONFIG_ACCEL_LSM6DSM_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* Power of 2 - Too large of a fifo causes too much timestamp jitter */
#define CONFIG_ACCEL_FIFO_SIZE 256
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_GMR_TABLET_MODE

/* TCPC */
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPM_RAA489000
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_5V_EN_CUSTOM

/* Thermistors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B
#define CONFIG_THROTTLE_AP
#define CONFIG_CHIPSET_CAN_THROTTLE

/* USB Mux and Retimer */
#define CONFIG_USB_MUX_IT5205 /* C1: ITE Mux */
#define I2C_PORT_USB_MUX I2C_PORT_USB_C0 /* Required for ITE Mux */

#define CONFIG_USBC_RETIMER_TUSB544 /* C1 Redriver: TUSB544 */

/* Keyboard */
#define CONFIG_KEYBOARD_VIVALDI
#define CONFIG_KEYBOARD_REFRESH_ROW3
#define CONFIG_KEYBOARD_KEYPAD

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum chg_id {
	CHARGER_PRIMARY,
	CHARGER_SECONDARY,
	CHARGER_NUM,
};

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_COUNT,
};

/* Motion sensors */
enum sensor_id { LID_ACCEL, BASE_ACCEL, BASE_GYRO, SENSOR_COUNT };

/* ADC channels */
enum adc_channel {
	ADC_VSNS_PP3300_A, /* ADC0 */
	ADC_TEMP_SENSOR_1, /* ADC2 */
	ADC_TEMP_SENSOR_2, /* ADC3 */
	ADC_TEMP_SENSOR_3, /* ADC15 */
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1,
	TEMP_SENSOR_2,
	TEMP_SENSOR_3,
	TEMP_SENSOR_COUNT
};

/* List of possible batteries */
enum battery_type {
	BATTERY_CA14J43,
	BATTERY_CA11J58,
	BATTERY_CA11J34,
	BATTERY_TYPE_COUNT,
};

enum battery_cell_type {
	BATTERY_CELL_TYPE_1S = 1,
	BATTERY_CELL_TYPE_2S = 2,
	BATTERY_CELL_TYPE_3S = 3,
	BATTERY_CELL_TYPE_4S = 4,
};

enum battery_cell_type board_get_battery_cell_type(void);
#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
