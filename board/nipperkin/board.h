/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/* Motion sensing drivers */

/* Keyboard features */
#define CONFIG_KEYBOARD_FACTORY_TEST

/* EC console commands */
#define CONFIG_CMD_BUTTON

/* USB Type C and USB PD defines */

/* USB Type A Features */

/* BC 1.2 */

/* Volume Button feature */

/* Fan features */

/* LED features */
#define CONFIG_LED_COMMON
#define CONFIG_LED_ONOFF_STATES

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* Battery Types */
enum battery_type {
	BATTERY_AEC,
	BATTERY_AP18F4M,
	BATTERY_POWER_TECH,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
