/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Bip board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_OCTOPUS_EC_ITE8320
#define VARIANT_OCTOPUS_CHARGER_BQ25703
#include "baseboard.h"

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

#define CONFIG_LED_COMMON

/* Sensors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B
#define CONFIG_STEINHART_HART_6V0_51K1_47K_4050B

/* Hardware for proto bip does not support ec keyboard backlight control. */
#undef CONFIG_PWM
#undef CONFIG_PWM_KBLIGHT

/* Old hardware does not support dedicated EC->AP interrupt for MKBP */
#define CONFIG_MKBP_USE_HOST_EVENT

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_VBUS_C0,
	ADC_VBUS_C1,
	ADC_TEMP_SENSOR_AMB,
	ADC_TEMP_SENSOR_CHARGER,
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,
	TEMP_SENSOR_AMBIENT,
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_COUNT
};

/* List of possible batteries */
enum battery_type {
	BATTERY_PANASONIC,
	BATTERY_SANYO,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
