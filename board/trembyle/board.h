/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trembyle board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "baseboard.h"

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

#define CONFIG_MKBP_USE_GPIO

/* Motion sensing drivers */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCEL_KX022
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_TABLET_MODE
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#ifndef __ASSEMBLER__

enum battery_type {
	BATTERY_PANASONIC,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */


#endif /* __CROS_EC_BOARD_H */
