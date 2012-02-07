/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery charger 1.1
 */
#ifndef __CROS_EC_SMART_BATTERY_H
#define __CROS_EC_SMART_BATTERY_H

/* Smart battery charger functions */
#define SB_CHARGER_SPEC_INFO		0x11
#define SB_CHARGE_MODE			0x12
#define SB_CHARGER_STATUS		0x13
#define SB_CHARGING_CURRENT		0x14
#define SB_CHARGING_VOLTAGE		0x15
#define SB_ALARM_WARNING		0x16

/* SB_ALARM_WARNING */
#define ALARM_OVER_CHARGE		0x8000
#define ALARM_TERMINATE_CHARG		0x4000
#define ALARM_RESERVED_2000		0x2000
#define ALARM_OVER_TEMP			0x1000
#define ALARM_TERMINATE_DISCHARGE	0x0800
#define ALARM_RESERVED_0400		0x0400
#define ALARM_REMAINING_CAPACITY	0x0200
#define ALARM_REMAINING_TIME		0x0100
#define ALARM_STATUS_INITIALIZE		0x0080
#define ALARM_STATUS_DISCHARGING	0x0040
#define ALARM_STATUS_FULLY_CHARGED	0x0020
#define ALARM_STATUS_FULLY_DISCHARG	0x0010
/* SB_CHARGE_MODE */
#define CHARGE_FLAG_INHIBIT_CHARGE	(1 << 0)
#define CHARGE_FLAG_ENABLE_POLLING	(1 << 1)
#define CHARGE_FLAG_POR_RESET		(1 << 2)
#define CHARGE_FLAG_RESET_TO_ZERO	(1 << 3)
/* SB_CHARGER_STATUS */
#define CHARGER_CHARGE_INHIBITED	(1 << 0)
#define CHARGER_POLLING_ENABLED		(1 << 1)
#define CHARGER_VOLTAGE_NOTREG		(1 << 2)
#define CHARGER_CURRENT_NOTREG		(1 << 3)
#define CHARGER_LEVEL_2			(1 << 4)
#define CHARGER_LEVEL_3			(1 << 5)
#define CHARGER_CURRENT_OR		(1 << 6)
#define CHARGER_VOLTAGE_OR		(1 << 7)
#define CHARGER_RES_OR			(1 << 8)
#define CHARGER_RES_COLD		(1 << 9)
#define CHARGER_RES_HOT			(1 << 10)
#define CHARGER_RES_UR			(1 << 11)
#define CHARGER_ALARM_INHIBITED		(1 << 12)
#define CHARGER_POWER_FAIL		(1 << 13)
#define CHARGER_BATTERY_PRESENT		(1 << 14)
#define CHARGER_AC_PRESENT		(1 << 15)
/* SB_CHARGER_SPEC_INFO */
#define INFO_CHARGER_SPEC(INFO)		((INFO) & 0xf)
#define INFO_SELECTOR_SUPPORT(INFO)	(((INFO) >> 4) & 1)

#endif /* __CROS_EC_SMART_BATTERY_H */

