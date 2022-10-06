/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dood board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_OCTOPUS_EC_NPCX796FB
#define VARIANT_OCTOPUS_CHARGER_ISL9238
#include "baseboard.h"

#define GPIO_PG_EC_RSMRST_ODL GPIO_RSMRST_L_PGOOD

/* I2C bus configuraiton */
#define I2C_PORT_ACCEL I2C_PORT_SENSOR

#define CONFIG_LED_COMMON

/* Sensors */
#define CONFIG_ACCEL_KX022 /* Lid accel */
#define CONFIG_ACCELGYRO_BMI160 /* Base accel */

#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

/* Motion Sense Task Events */
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

#define CONFIG_SYNC_INT_EVENT TASK_EVENT_MOTION_SENSOR_INTERRUPT(VSYNC)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L GPIO_EC_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_EC_VOLDN_BTN_ODL

#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_13K7_47K_4050B
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* Additional PPC second source */
#define CONFIG_USBC_PPC_SYV682X
#define CONFIG_USBC_PPC_DEDICATED_INT
#undef CONFIG_SYV682X_HV_ILIM
#define CONFIG_SYV682X_HV_ILIM SYV682X_HV_ILIM_5_50
/* SYV682 isn't connected to CC, so TCPC must provide VCONN */
#define CONFIG_USBC_PPC_SYV682X_NO_CC

/* prevent pd reset when battery soc under 2% */
#define CONFIG_USB_PD_RESET_MIN_BATT_SOC 2

#ifndef __ASSEMBLER__

/* support factory keyboard test */
#define CONFIG_KEYBOARD_FACTORY_TEST

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_AMB, /* ADC0 */
	ADC_TEMP_SENSOR_CHARGER, /* ADC1 */
	ADC_VBUS_C0, /* ADC9 */
	ADC_VBUS_C1, /* ADC4 */
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,
	TEMP_SENSOR_AMBIENT,
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_COUNT
};

/* Motion sensors */
enum sensor_id { LID_ACCEL, BASE_ACCEL, BASE_GYRO, SENSOR_COUNT };

/* List of possible batteries */
enum battery_type {
	BATTERY_LGC15,
	BATTERY_PANASONIC_AP15O5L,
	BATTERY_SANYO,
	BATTERY_SONY,
	BATTERY_SMP_AP13J7K,
	BATTERY_PANASONIC_AC15A3J,
	BATTERY_LGC_AP18C8K,
	BATTERY_MURATA_AP18C4K,
	BATTERY_LGC_AP19A8K,
	BATTERY_LGC_G023,
	BATTERY_SMP_PCVPBP144,
	BATTERY_SMP_PCVPBP126,
	BATTERY_SMP_PCVPBP136,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
