/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Krabby board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "baseboard.h"

/* Chipset config */

/* Optional features */
#define CONFIG_LTO

/*
 * TODO: Remove this option once the VBAT no longer keeps high when
 * system's power isn't presented.
 */
#define CONFIG_IT83XX_RESET_PD_CONTRACT_IN_BRAM

/* BC12 */

/* LED */
#define CONFIG_LED_ONOFF_STATES
#define CONFIG_LED_ONOFF_STATES_BAT_LOW 10

/* PD / USB-C / PPC */
#define CONFIG_USB_PD_DEBUG_LEVEL 3
#define PD_MAX_CURRENT_MA 3000
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_VOLTAGE_MV 15000
#define PD_MAX_POWER_MW 45000
#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

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
#define CONFIG_ACCELGYRO_ICM426XX /* Base accel */
#define CONFIG_ACCELGYRO_ICM426XX_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

#define CONFIG_ACCEL_LIS2DWL
#define CONFIG_ACCEL_LIS2DW_AS_BASE
#define CONFIG_ACCEL_LIS2DW12_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_LID_ANGLE_UPDATE

#define CONFIG_ACCEL_FORCE_MODE_MASK 0

/* SPI / Host Command */
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* USB-A */
#define USBA_PORT_COUNT 1

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum battery_type {
	BATTERY_C235,
	BATTERY_TYPE_COUNT,
};

enum sensor_id {
	BASE_ACCEL = 0,
	BASE_GYRO,
	LID_ACCEL,
	SENSOR_COUNT,
};

enum adc_channel {
	ADC_VBUS_C0,             /* ADC 0 */
	ADC_BOARD_ID_0,          /* ADC 1 */
	ADC_BOARD_ID_1,          /* ADC 2 */
	ADC_CHARGER_AMON_R,      /* ADC 3 */
	ADC_VBUS_C1,             /* ADC 5 */
	ADC_CHARGER_PMON,        /* ADC 6 */

	/* Number of ADC channels */
	ADC_CH_COUNT,
};

enum pwm_channel {
	PWM_CH_LED1,
	PWM_CH_LED2,
	PWM_CH_LED3,
	PWM_CH_COUNT,
};

void motion_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
