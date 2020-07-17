/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fleex board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_OCTOPUS_EC_NPCX796FB
#define VARIANT_OCTOPUS_CHARGER_ISL9238
#include "baseboard.h"

/* EC console commands  */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

#define CONFIG_LED_COMMON
#define CONFIG_LED_ONOFF_STATES_BAT_LOW 10

/*
 * Some fuel gagues will return 1% immediately, without the battery being
 * charged to the point of being able to withstand Vbus loss, so re-set
 * allowable Try.SRC level and reset level to 2%
 */
#undef CONFIG_USB_PD_TRY_SRC_MIN_BATT_SOC
#define CONFIG_USB_PD_TRY_SRC_MIN_BATT_SOC 2

#define CONFIG_USB_PD_RESET_MIN_BATT_SOC 2

/* Sensors */
#define CONFIG_ACCEL_LIS2DE		/* Lid accel */
#define CONFIG_ACCELGYRO_LSM6DSM	/* Base accel */
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

/* Volume button */
#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L GPIO_EC_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_EC_VOLDN_BTN_ODL

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_13K7_47K_4050B
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* TI gauge IC 500ms WDT timeout setting under battery sleep mode
 * induced battery cut-off, under the following conditions:
 * 1. SMBus communication on FC is once per minute which allows
 * battery entering sleep mode;
 * 2. System load < 10mA and accumulate 5 hours will trigger battery
 * simulation and result in a 500ms WDT timeout. So change charge
 * max sleep time from once/minute to once/10 seconds to prevent
 * battery entering sleep mode. See b/133375756.
 */
#define CHARGE_MAX_SLEEP_USEC (10 * SECOND)

#define CONFIG_ACCEL_LSM6DSM_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_AMB,		/* ADC0 */
	ADC_TEMP_SENSOR_CHARGER,	/* ADC1 */
	ADC_VBUS_C0,			/* ADC9 */
	ADC_VBUS_C1,			/* ADC4 */
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,
	TEMP_SENSOR_AMBIENT,
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_COUNT
};

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_COUNT
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL,
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT
};

/* List of possible batteries */
enum battery_type {
	BATTERY_BYD,
	BATTERY_BYD16,
	BATTERY_LGC,
	BATTERY_LGC3,
	BATTERY_SIMPLO,
	BATTERY_SIMPLO_ATL,
	BATTERY_SIMPLO_COS,
	BATTERY_SIMPLO_LS,
	BATTERY_SWD_ATL,
	BATTERY_SWD_COS,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
