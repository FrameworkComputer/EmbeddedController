/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Goroh board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "baseboard.h"

/* Chipset config */
#define CONFIG_BRINGUP

/* Optional features */
#define CONFIG_LTO

/*
 * TODO: Remove this option once the VBAT no longer keeps high when
 * system's power isn't presented.
 */
#define CONFIG_IT83XX_RESET_PD_CONTRACT_IN_BRAM

/* LED */
#define CONFIG_LED_POWER_LED
#define CONFIG_LED_ONOFF_STATES
#define CONFIG_LED_ONOFF_STATES_BAT_LOW 10

/* PD / USB-C / PPC */
#define CONFIG_USB_PD_DEBUG_LEVEL 3

/* Optional console commands */
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_SCRATCHPAD
#define CONFIG_CMD_STACKOVERFLOW

#define CONFIG_BATT_FULL_CHIPSET_OFF_INPUT_LIMIT_MV	9000

/* Sensor */
#define CONFIG_GMR_TABLET_MODE
#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define GMR_TABLET_MODE_GPIO_L GPIO_TABLET_MODE_L

#define CONFIG_ACCELGYRO_BMI160 /* Base accel */
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_LID_ANGLE_UPDATE

#define CONFIG_ACCEL_BMA255 /* Lid accel BMA253 */

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK \
	(BIT(LID_ACCEL) | BIT(BASE_GYRO) | BIT(BASE_ACCEL))

/* SPI / Host Command */
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* USB-A */
#define USBA_PORT_COUNT 1

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum battery_type {
	BATTERY_LGC_AP18C8K,
	BATTERY_TYPE_COUNT,
};

enum sensor_id {
	BASE_ACCEL = 0,
	BASE_GYRO,
	LID_ACCEL,

	SENSOR_COUNT,
};

enum adc_channel {
	ADC_BOARD_ID_0,     /* ADC 1 */
	ADC_TEMP_VDD_CPU,   /* ADC 2 */
	ADC_TEMP_VDD_GPU,   /* ADC 3 */
	ADC_TEMP_CHARGER,   /* ADC 5 */

	/* Number of ADC channels */
	ADC_CH_COUNT,
};

enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
	USBC_PORT_COUNT
};

enum pwm_channel {
	PWM_CH_LED1,
	PWM_CH_LED2,
	PWM_CH_FAN,
	PWM_CH_KB_BL,
	PWM_CH_COUNT,
};

enum fan_channel {
	FAN_CH_0,
	FAN_CH_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
