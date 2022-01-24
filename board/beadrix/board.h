/* Copyright 2021 The Chromium OS Authors. All rights reserved.
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
#define CONFIG_SYSTEM_UNLOCKED

/* EC console commands */
#define CONFIG_CMD_CHARGER_DUMP

/* Battery */
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_LOW_VOLTAGE_PROTECTION

/* BC 1.2 */
#define CONFIG_BC12_DETECT_PI3USB9201

/* Charger */
#define CONFIG_CHARGER_RAA489000
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGE_RAMP_HW
#undef  CONFIG_CHARGER_SINGLE_CHIP
#define CONFIG_OCPC
#define CONFIG_OCPC_DEF_RBATT_MOHMS 22 /* R_DS(on) 11.6mOhm + 10mOhm sns rstr */

/*
 * GPIO for C1 interrupts, for baseboard use
 *
 * Note this will only be valid for board revision 1
 */
#define GPIO_USB_C1_INT_ODL GPIO_USB_C1_INT_V1_ODL

/* PWM */
#define CONFIG_PWM

/* Sensors */
#define CONFIG_ACCEL_KX022		/* Lid accel */
#define CONFIG_ACCELGYRO_LSM6DSM	/* Base accel */
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

#define CONFIG_ACCEL_INTERRUPTS
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
#define CONFIG_USB_PD_TCPM_ANX7447	/* C1: MUX  only*/
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_ANX7447_AUX_PU_PD

/* Thermistors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* USB */
#define CONFIG_BC12_DETECT_PI3USB9201
#define CONFIG_USB_MUX_RUNTIME_CONFIG
#define CONFIG_USB_MUX_IT5205	/* C0: ITE MUX */

/* USB PD */
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPM_RAA489000 /* C1: TCPC + Charger */
#undef CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (100 * MSEC)

/* USB Mux and Retimer */
#define CONFIG_USB_MUX_RUNTIME_CONFIG
#define CONFIG_USB_MUX_IT5205			/* C1: ITE Mux */
#define CONFIG_USBC_RETIMER_NB7V904M

/* USB defines specific to external TCPCs */
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_TCPC_LOW_POWER

/* Variant references the TCPCs to determine Vbus sourcing */
#define CONFIG_USB_PD_5V_EN_CUSTOM

#undef PD_POWER_SUPPLY_TURN_ON_DELAY
#undef PD_POWER_SUPPLY_TURN_OFF_DELAY
/* 20% margin added for these timings */
#define PD_POWER_SUPPLY_TURN_ON_DELAY	13080	/* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	16080	/* us */

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
enum sensor_id {
	LID_ACCEL,
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT
};

/* ADC channels */
enum adc_channel {
	ADC_VSNS_PP3300_A,     /* ADC0 */
	ADC_TEMP_SENSOR_1,     /* ADC2 */
	ADC_TEMP_SENSOR_2,     /* ADC3 */
	ADC_SUB_ANALOG,        /* ADC13 */
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1,
	TEMP_SENSOR_2,
	TEMP_SENSOR_COUNT
};

/* List of possible batteries */
enum battery_type {
	BATTERY_GANFENG,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
