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

/* Charger*/
#define CONFIG_CHARGER_MAX_INPUT_CURRENT 3100
#define CONFIG_CHARGER_PROFILE_OVERRIDE

/* PD / USB-C / PPC */
#define CONFIG_USB_PD_DEBUG_LEVEL 3
#define PD_MAX_POWER_MW 65000
#define PD_MAX_CURRENT_MA CONFIG_CHARGER_MAX_INPUT_CURRENT
#define PD_MAX_VOLTAGE_MV 20000
#define PD_OPERATING_POWER_MW 15000
#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */
#undef CONFIG_SYV682X_HV_ILIM
#define CONFIG_SYV682X_HV_ILIM SYV682X_HV_ILIM_5_50

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
