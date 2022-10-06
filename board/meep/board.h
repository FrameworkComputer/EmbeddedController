/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Meep/Mimrock board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_OCTOPUS_EC_NPCX796FB
#define VARIANT_OCTOPUS_CHARGER_ISL9238
#include "baseboard.h"

#define GPIO_PG_EC_RSMRST_ODL GPIO_RSMRST_L_PGOOD

#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L GPIO_EC_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_EC_VOLDN_BTN_ODL

#define CONFIG_LED_COMMON

/* Sensors */
#define CONFIG_ACCEL_KX022 /* Lid accel */
#define CONFIG_ACCELGYRO_LSM6DSM /* Base accel */

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_PWM
#define CONFIG_PWM_KBLIGHT
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_13K7_47K_4050B
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

#define CONFIG_KEYBOARD_FACTORY_TEST

#define CONFIG_LED_ONOFF_STATES_BAT_LOW 10

#define CONFIG_ACCEL_LSM6DSM_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

/* Additional PPC second source */
#define CONFIG_USBC_PPC_SYV682X
#define CONFIG_USBC_PPC_DEDICATED_INT
#undef CONFIG_SYV682X_HV_ILIM
#define CONFIG_SYV682X_HV_ILIM SYV682X_HV_ILIM_5_50
/* SYV682 isn't connected to CC, so TCPC must provide VCONN */
#define CONFIG_USBC_PPC_SYV682X_NO_CC

/* Additional TCPC second source in Port 1 */
#define CONFIG_USB_PD_TCPM_MULTI_PS8XXX
#define CONFIG_USB_PD_TCPM_PS8755

#ifndef __ASSEMBLER__

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

enum pwm_channel { PWM_CH_KBLIGHT, PWM_CH_COUNT };

/* Motion sensors */
enum sensor_id { LID_ACCEL, BASE_ACCEL, BASE_GYRO, SENSOR_COUNT };

enum battery_type {
	BATTERY_DYNAPACK_COS,
	BATTERY_DYNAPACK_ATL,
	BATTERY_DYNAPACK_SDI,
	BATTERY_SAMSUNG_SDI,
	BATTERY_SIMPLO_COS,
	BATTERY_SIMPLO_ATL,
	BATTERY_SIMPLO_HIGHPOWER,
	BATTERY_COS,
	BATTERY_TYPE_COUNT,
};

enum ppc_type {
	PPC_NX20P348X,
	PPC_SYV682X,
	PPC_TYPE_COUNT,
};

int board_is_convertible(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
