/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kracko board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_DEDEDE_EC_IT8320
#include "baseboard.h"

#undef GPIO_VOLUME_UP_L
#define GPIO_VOLUME_UP_L GPIO_VOLUP_BTN_ODL_HDMI_HPD

/* Battery */
#define CONFIG_BATTERY_FUEL_GAUGE

/* BC 1.2 */
#define CONFIG_BC12_DETECT_PI3USB9201

/* Charger */
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_CHARGER_SM5803 /* C0 and C1: Charger */
#define PD_MAX_VOLTAGE_MV 15000
#define CONFIG_USB_PD_VBUS_DETECT_CHARGER
#define CONFIG_USB_PD_5V_CHARGER_CTRL
#define CONFIG_CHARGER_OTG
#undef CONFIG_CHARGER_SINGLE_CHIP
#define CONFIG_OCPC
#define CONFIG_OCPC_DEF_RBATT_MOHMS               \
	21 /* R_DS(on) 10.7mOhm + 10mOhm sns rstr \
	    */

/* PWM */
#define CONFIG_PWM

/* LED defines */
#define CONFIG_LED_ONOFF_STATES

/* Sensors */
#define CONFIG_ACCEL_BMA255 /* Lid accel */
#define CONFIG_ACCEL_KX022 /* Lid accel */
#define CONFIG_ACCEL_BMA4XX /* Lid accel */
#define CONFIG_ACCELGYRO_LSM6DSM /* Base accel */
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* Power of 2 - Too large of a fifo causes too much timestamp jitter */
#define CONFIG_ACCEL_FIFO_SIZE 256
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_ACCEL_LSM6DSM_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_GMR_TABLET_MODE

/* Keyboard */
#define CONFIG_KEYBOARD_FACTORY_TEST
#define CONFIG_PWM_KBLIGHT

/* TCPC */
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPM_ITE_ON_CHIP /* C0: ITE EC TCPC */
#define CONFIG_USB_PD_TCPM_MULTI_PS8XXX
#define CONFIG_USB_PD_TCPM_PS8705 /* C1: PS8705 TCPC */
#define CONFIG_USB_PD_TCPM_PS8805 /* C1: second source PS8805 TCPC */
#define CONFIG_USB_PD_TCPM_PS8805_FORCE_DID
#define CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT 1
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_TCPC_LOW_POWER

/* Thermistors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* USB Mux and Retimer */
#define CONFIG_USB_MUX_IT5205 /* C1: ITE Mux */
#define I2C_PORT_USB_MUX I2C_PORT_USB_C0 /* Required for ITE Mux */

/* USB Type A Features */
#define USB_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_DUMB

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
	ADC_SUB_ANALOG, /* ADC13 */
	ADC_TEMP_SENSOR_3, /* ADC15 */
	ADC_TEMP_SENSOR_4, /* ADC16 */
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
	BATTERY_SIMPLO,
	BATTERY_COS,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
