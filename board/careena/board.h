/* Copyright 2018 The Chromium OS Authors. All rights reserved.
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
#define CC_DEFAULT     (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_LPC)))
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

#define CONFIG_MKBP_USE_HOST_EVENT

#define CONFIG_LED_COMMON
#define CONFIG_CMD_LEDTEST
#define CONFIG_KEYBOARD_FACTORY_TEST

#define CONFIG_BATTERY_MEASURE_IMBALANCE
#define CONFIG_BATTERY_BQ4050

/* Additional TCPC second source in Port 1 */
#define CONFIG_USB_PD_TCPM_MULTI_PS8XXX
#define CONFIG_USB_PD_TCPM_PS8755

#ifndef __ASSEMBLER__

enum pwm_channel {
	PWM_CH_KBLIGHT = 0,
	PWM_CH_COUNT
};

enum battery_type {
	BATTERY_DANAPACK_COS,
	BATTERY_DANAPACK_ATL,
	BATTERY_DANAPACK_SDI,
	BATTERY_SAMSUNG_SDI,
	BATTERY_SIMPLO_COS,
	BATTERY_SIMPLO_ATL,
	BATTERY_SIMPLO_HIGHPOWER,
	BATTERY_TYPE_COUNT,
};

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
extern const int keyboard_factory_scan_pins[][2];
extern const int keyboard_factory_scan_pins_used;
#endif

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
