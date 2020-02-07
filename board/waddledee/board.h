/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledee board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_DEDEDE_EC_IT8320
#include "baseboard.h"

/* Undef battery, charger, LED, PD functions until charger is in */
#undef CONFIG_BATTERY_CUT_OFF
#undef CONFIG_BATTERY_PRESENT_GPIO
#undef CONFIG_BATTERY_REVIVE_DISCONNECT
#undef CONFIG_BATTERY_SMART
#undef CONFIG_CHARGE_MANAGER
#undef CONFIG_CHARGER
#undef CONFIG_CHARGER_INPUT_CURRENT
#undef CONFIG_LED_COMMON
#undef CONFIG_LED_PWM
#undef CONFIG_USB_MUX_PI3USB31532
#undef CONFIG_USBC_SS_MUX
#undef CONFIG_USBC_SS_MUX_DFP_ONLY
#undef CONFIG_USBC_VCONN
#undef CONFIG_USBC_VCONN_SWAP
#undef CONFIG_USB_CHARGER
#undef CONFIG_USB_PD_5V_EN_CUSTOM
#undef CONFIG_TRICKLE_CHARGING
#undef CONFIG_USB_PD_ALT_MODE
#undef CONFIG_USB_PD_ALT_MODE_DFP
#undef CONFIG_USB_PD_DISCHARGE_TCPC
#undef CONFIG_USB_PD_DP_HPD_GPIO
#undef CONFIG_USB_PD_DUAL_ROLE
#undef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#undef CONFIG_USB_PD_LOGGING
#undef CONFIG_USB_PD_PORT_MAX_COUNT
#undef CONFIG_USB_PD_TCPM_MUX
#undef CONFIG_USB_PD_TCPM_TCPCI
#undef CONFIG_USB_PD_TRY_SRC
#undef CONFIG_USB_PD_VBUS_DETECT_TCPC
#undef CONFIG_USB_PD_VBUS_MEASURE_CHARGER
#undef CONFIG_USB_PD_DECODE_SOP
#undef CONFIG_USB_PID
#undef CONFIG_USB_POWER_DELIVERY
#undef CONFIG_USB_SM_FRAMEWORK
#undef CONFIG_USB_TYPEC_DRP_ACC_TRYSRC

/* System unlocked in early development */
#define CONFIG_SYSTEM_UNLOCKED

/* Sensors */
#define CONFIG_ACCEL_LIS2DE		/* Lid accel */
#define CONFIG_ACCELGYRO_LSM6DSM	/* Base accel */
#define CONFIG_SYNC			/* Camera VSYNC */
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

#define CONFIG_SYNC_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(VSYNC)

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_GMR_TABLET_MODE

/* Thermistors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B
#define CONFIG_TEMP_SENSOR_POWER_GPIO GPIO_EN_PP3300_A

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_LED_RED,
	PWM_CH_LED_GREEN,
	PWM_CH_LED_BLUE,
	PWM_CH_COUNT,
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL,
	BASE_ACCEL,
	BASE_GYRO,
	VSYNC,
	SENSOR_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1,
	TEMP_SENSOR_2,
	TEMP_SENSOR_COUNT
};

/* List of possible batteries */
enum battery_type {
	/* TODO(b/146557556): get battery spec(s) */
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
