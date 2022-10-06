/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Careena board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_GRUNT_TCPC_0_ANX3429
#define VARIANT_GRUNT_NO_SENSORS

#include "baseboard.h"

/*
 * By default, enable all console messages excepted HC, ACPI and event:
 * The sensor stack is generating a lot of activity.
 */
#define CC_DEFAULT (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

#define CONFIG_MKBP_USE_HOST_EVENT

#define CONFIG_LED_COMMON
#define CONFIG_CMD_LEDTEST
#define CONFIG_KEYBOARD_FACTORY_TEST

/* Thermal */
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define CONFIG_THROTTLE_AP

#define CONFIG_BATTERY_MEASURE_IMBALANCE
#define CONFIG_BATTERY_BQ4050

/* Additional TCPC second source in Port 1 */
#define CONFIG_USB_PD_TCPM_MULTI_PS8XXX
#define CONFIG_USB_PD_TCPM_PS8755

#ifndef __ASSEMBLER__

enum pwm_channel { PWM_CH_KBLIGHT = 0, PWM_CH_COUNT };

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

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
