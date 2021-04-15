/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Spherion board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "baseboard.h"

/* Chipset config */

/* Optional features */
#define CONFIG_LTO
#undef CONFIG_LOW_POWER_S0

/*
 * TODO: Remove this option once the VBAT no longer keeps high when
 * system's power isn't presented.
 */
#define CONFIG_IT83XX_RESET_PD_CONTRACT_IN_BRAM

/* Temperature sensor */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_30K9_47K_4050B

/* LED */
#define CONFIG_LED_ONOFF_STATES

/* Keyboard features */
#define CONFIG_KEYBOARD_REFRESH_ROW3

/* Keyboard backliht */
#define CONFIG_PWM_KBLIGHT

/* PD / USB-C / PPC */
#define CONFIG_USB_PD_DEBUG_LEVEL 3

/* Optional console commands */
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_SCRATCHPAD
#define CONFIG_CMD_STACKOVERFLOW

/* Sensor */

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
	BATTERY_PANASONIC_AP15O5L,
	BATTERY_TYPE_COUNT,
};

enum temp_sensor_id {
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_COUNT
};

enum adc_channel {
	ADC_VBUS_C0,             /* ADC 0 */
	ADC_BOARD_ID_0,          /* ADC 1 */
	ADC_BOARD_ID_1,          /* ADC 2 */
	ADC_CHARGER_AMON_R,      /* ADC 3 */
	ADC_VBUS_C1,             /* ADC 5 */
	ADC_CHARGER_PMON,        /* ADC 6 */
	ADC_TEMP_SENSOR_CHARGER, /* ADC 7 */
	/* Number of ADC channels */
	ADC_CH_COUNT,
};

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_COUNT,
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
