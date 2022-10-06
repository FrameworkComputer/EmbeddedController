/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Phaser board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_OCTOPUS_EC_NPCX796FB
#define VARIANT_OCTOPUS_CHARGER_ISL9238
#include "baseboard.h"

/* b/203442963
 * It's workaround to reduce keyboard's "Silver Migration".
 * From keyboard vendor's feedback, there are two factors to cause
 * "Silver Migration".
 * 1. A voltage potential between trace.
 * 2. The presence of an electrolyte , such as moisture.
 * The reason cause voltage potential between KSIxx trace is EC enter ec
 * hibernate PSL and turn EC's VCC1 power off. Besides KSI2, the other
 * KSIxx will be turn off. KSI2 is powered by H1.
 * To avoid voltage potential is keep KSIxx on. That means not to enter
 * ec hibernate PSL.
 */
#undef CONFIG_HIBERNATE_PSL

#define GPIO_PG_EC_RSMRST_ODL GPIO_RSMRST_L_PGOOD

#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L GPIO_EC_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_EC_VOLDN_BTN_ODL

#define CONFIG_LED_COMMON
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_13K7_47K_4050B
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* EC console commands  */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

/* Sensors */
#define CONFIG_ACCEL_LIS2DE /* Lid accel */
#define CONFIG_ACCELGYRO_LSM6DSM /* Base accel */
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

#define CONFIG_ACCEL_LSM6DSM_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

/* Additional PPC second source */
#define CONFIG_USBC_PPC_SYV682X
#define CONFIG_USBC_PPC_DEDICATED_INT
#undef CONFIG_SYV682X_HV_ILIM
#define CONFIG_SYV682X_HV_ILIM SYV682X_HV_ILIM_5_50
/* SYV682 isn't connected to CC, so TCPC must provide VCONN */
#define CONFIG_USBC_PPC_SYV682X_NO_CC

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_AMB, /* ADC0 */
	ADC_TEMP_SENSOR_CHARGER, /* ADC1 */
	ADC_VBUS_C0, /* ADC9 */
	ADC_VBUS_C1, /* ADC4 */
	ADC_CH_COUNT,
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,
	TEMP_SENSOR_AMBIENT,
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_COUNT
};

enum pwm_channel { PWM_CH_KBLIGHT, PWM_CH_COUNT };

/* Motion sensors */
enum sensor_id { LID_ACCEL, BASE_ACCEL, BASE_GYRO, SENSOR_COUNT };

/* List of possible batteries */
enum battery_type {
	BATTERY_PANASONIC,
	BATTERY_SMP,
	BATTERY_LGC,
	BATTERY_SUNWODA,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
