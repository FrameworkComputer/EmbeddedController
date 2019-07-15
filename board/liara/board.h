/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Liara board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_GRUNT_TCPC_0_ANX3429
#define VARIANT_GRUNT_NO_SENSORS

#include "baseboard.h"

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CC_DEFAULT     (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

#define CONFIG_MKBP_USE_HOST_EVENT

/* Power and battery LEDs */
#define CONFIG_LED_COMMON
#define CONFIG_LED_PWM_CHARGE_STATE_ONLY
#define CONFIG_CMD_LEDTEST

#undef CONFIG_LED_PWM_NEAR_FULL_COLOR
#undef CONFIG_LED_PWM_CHARGE_ERROR_COLOR

#define CONFIG_LED_PWM_NEAR_FULL_COLOR EC_LED_COLOR_WHITE
#define CONFIG_LED_PWM_CHARGE_ERROR_COLOR EC_LED_COLOR_AMBER

#define CONFIG_LED_PWM_COUNT 1

#define I2C_PORT_KBLIGHT NPCX_I2C_PORT5_0

/* KB backlight driver */
#define CONFIG_LED_DRIVER_LM3630A

#define CONFIG_BATTERY_BQ4050
#define CONFIG_BATTERY_MEASURE_IMBALANCE

#ifndef __ASSEMBLER__

enum pwm_channel {
	PWM_CH_KBLIGHT = 0,
	PWM_CH_LED1_WHITE,
	PWM_CH_LED2_AMBER,
	PWM_CH_COUNT
};

enum battery_type {
	BATTERY_PANASONIC,
	BATTERY_SUNWODA,
	BATTERY_SIMPLO,
	BATTERY_LGC,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
