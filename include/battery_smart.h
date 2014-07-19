/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery v1.0
 * Smart battery charger v1.1
 */
#ifndef __CROS_EC_SMART_BATTERY_H
#define __CROS_EC_SMART_BATTERY_H

#include "common.h"

/* Smart battery and charger I2C address */
#define BATTERY_ADDR 0x16
#define CHARGER_ADDR 0x12

/* Charger functions */
#define SB_CHARGER_SPEC_INFO            0x11
#define SB_CHARGE_MODE                  0x12
#define SB_CHARGER_STATUS               0x13
#define SB_CHARGING_CURRENT             0x14
#define SB_CHARGING_VOLTAGE             0x15
#define SB_ALARM_WARNING                0x16

/* Battery functions */
#define SB_MANUFACTURER_ACCESS          0x00
#define SB_REMAINING_CAPACITY_ALARM     0x01
#define SB_REMAINING_TIME_ALARM         0x02
#define SB_BATTERY_MODE                 0x03
#define SB_AT_RATE                      0x04
#define SB_AT_RATE_TIME_TO_FULL         0x05
#define SB_AT_RATE_TIME_TO_EMPTY        0x06
#define SB_AT_RATE_OK                   0x07
#define SB_TEMPERATURE                  0x08
#define SB_VOLTAGE                      0x09
#define SB_CURRENT                      0x0a
#define SB_AVERAGE_CURRENT              0x0b
#define SB_MAX_ERROR                    0x0c
#define SB_RELATIVE_STATE_OF_CHARGE     0x0d
#define SB_ABSOLUTE_STATE_OF_CHARGE     0x0e
#define SB_REMAINING_CAPACITY           0x0f
#define SB_FULL_CHARGE_CAPACITY         0x10
#define SB_RUN_TIME_TO_EMPTY            0x11
#define SB_AVERAGE_TIME_TO_EMPTY        0x12
#define SB_AVERAGE_TIME_TO_FULL         0x13
#define SB_CHARGING_CURRENT             0x14
#define SB_CHARGING_VOLTAGE             0x15
#define SB_BATTERY_STATUS               0x16
#define SB_CYCLE_COUNT                  0x17
#define SB_DESIGN_CAPACITY              0x18
#define SB_DESIGN_VOLTAGE               0x19
#define SB_SPECIFICATION_INFO           0x1a
#define SB_MANUFACTURER_DATE            0x1b
#define SB_SERIAL_NUMBER                0x1c
#define SB_MANUFACTURER_NAME            0x20
#define SB_DEVICE_NAME                  0x21
#define SB_DEVICE_CHEMISTRY             0x22
#define SB_MANUFACTURER_DATA            0x23
/* Extention of smart battery spec, may not be supported on all platforms */
#define SB_ALT_MANUFACTURER_ACCESS      0x44

/* Battery mode */
#define MODE_INTERNAL_CHARGE_CONTROLLER (1 << 0)
#define MODE_PRIMARY_BATTERY_SUPPORT    (1 << 1)
#define MODE_CONDITION_CYCLE            (1 << 7)
#define MODE_CHARGE_CONTROLLER_ENABLED  (1 << 8)
#define MODE_PRIMARY_BATTERY            (1 << 9)
#define MODE_ALARM                      (1 << 13)
#define MODE_CHARGER                    (1 << 14)
#define MODE_CAPACITY                   (1 << 15)

/* Battery status */
#define STATUS_ERR_CODE_MASK            0xf
#define STATUS_CODE_OK                  0
#define STATUS_CODE_BUSY                1
#define STATUS_CODE_RESERVED            2
#define STATUS_CODE_UNSUPPORTED         3
#define STATUS_CODE_ACCESS_DENIED       4
#define STATUS_CODE_OVERUNDERFLOW       5
#define STATUS_CODE_BADSIZE             6
#define STATUS_CODE_UNKNOWN_ERROR       7
#define STATUS_FULLY_DISCHARGED         (1 << 4)
#define STATUS_FULLY_CHARGED            (1 << 5)
#define STATUS_DISCHARGING              (1 << 6)
#define STATUS_INITIALIZED              (1 << 7)
#define STATUS_REMAINING_TIME_ALARM     (1 << 8)
#define STATUS_REMAINING_CAPACITY_ALARM (1 << 9)
#define STATUS_TERMINATE_DISCHARGE_ALARM (1 << 11)
#define STATUS_OVERTEMP_ALARM           (1 << 12)
#define STATUS_TERMINATE_CHARGE_ALARM   (1 << 14)
#define STATUS_OVERCHARGED_ALARM        (1 << 15)

/* Charger alarm warning */
#define ALARM_OVER_CHARGED              0x8000
#define ALARM_TERMINATE_CHARGE          0x4000
#define ALARM_RESERVED_2000             0x2000
#define ALARM_OVER_TEMP                 0x1000
#define ALARM_TERMINATE_DISCHARGE       0x0800
#define ALARM_RESERVED_0400             0x0400
#define ALARM_REMAINING_CAPACITY        0x0200
#define ALARM_REMAINING_TIME            0x0100
#define ALARM_STATUS_INITIALIZE         0x0080
#define ALARM_STATUS_DISCHARGING        0x0040
#define ALARM_STATUS_FULLY_CHARGED      0x0020
#define ALARM_STATUS_FULLY_DISCHARGED   0x0010
/* Charge mode */
#define CHARGE_FLAG_INHIBIT_CHARGE      (1 << 0)
#define CHARGE_FLAG_ENABLE_POLLING      (1 << 1)
#define CHARGE_FLAG_POR_RESET           (1 << 2)
#define CHARGE_FLAG_RESET_TO_ZERO       (1 << 3)
/* Charger status */
#define CHARGER_CHARGE_INHIBITED        (1 << 0)
#define CHARGER_POLLING_ENABLED         (1 << 1)
#define CHARGER_VOLTAGE_NOTREG          (1 << 2)
#define CHARGER_CURRENT_NOTREG          (1 << 3)
#define CHARGER_LEVEL_2                 (1 << 4)
#define CHARGER_LEVEL_3                 (1 << 5)
#define CHARGER_CURRENT_OR              (1 << 6)
#define CHARGER_VOLTAGE_OR              (1 << 7)
#define CHARGER_RES_OR                  (1 << 8)
#define CHARGER_RES_COLD                (1 << 9)
#define CHARGER_RES_HOT                 (1 << 10)
#define CHARGER_RES_UR                  (1 << 11)
#define CHARGER_ALARM_INHIBITED         (1 << 12)
#define CHARGER_POWER_FAIL              (1 << 13)
#define CHARGER_BATTERY_PRESENT         (1 << 14)
#define CHARGER_AC_PRESENT              (1 << 15)
/* Charger specification info */
#define INFO_CHARGER_SPEC(INFO)         ((INFO) & 0xf)
#define INFO_SELECTOR_SUPPORT(INFO)     (((INFO) >> 4) & 1)

/* Manufacturer Access parameters */
#define PARAM_SAFETY_STATUS             0x51
#define PARAM_OPERATION_STATUS          0x54
/* Operation status masks -- 6 byte reply */
/* reply[3] */
#define BATTERY_DISCHARGING_DISABLED    0x20
#define BATTERY_CHARGING_DISABLED       0x40

/* Read from charger */
int sbc_read(int cmd, int *param);

/* Write to charger */
int sbc_write(int cmd, int param);

/* Read from battery */
int sb_read(int cmd, int *param);

/* Read sequence from battery */
int sb_read_string(int port, int slave_addr, int offset, uint8_t *data,
		   int len);

/* Write to battery */
int sb_write(int cmd, int param);

#endif /* __CROS_EC_SMART_BATTERY_H */

