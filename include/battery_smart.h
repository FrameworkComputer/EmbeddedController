/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery v1.0
 * Smart battery charger v1.1
 */
#ifndef __CROS_EC_BATTERY_SMART_H
#define __CROS_EC_BATTERY_SMART_H

#include "common.h"

/* Smart battery and charger I2C address */
#define BATTERY_ADDR_FLAGS 0x0B
#define CHARGER_ADDR_FLAGS 0x09

/* Charger functions */
#define SB_CHARGER_SPEC_INFO 0x11
#define SB_CHARGE_MODE 0x12
#define SB_CHARGER_STATUS 0x13
#define SB_CHARGING_CURRENT 0x14
#define SB_CHARGING_VOLTAGE 0x15
#define SB_ALARM_WARNING 0x16

/* Battery functions */
#define SB_MANUFACTURER_ACCESS 0x00
#define SB_REMAINING_CAPACITY_ALARM 0x01
#define SB_REMAINING_TIME_ALARM 0x02
#define SB_BATTERY_MODE 0x03
#define SB_AT_RATE 0x04
#define SB_AT_RATE_TIME_TO_FULL 0x05
#define SB_AT_RATE_TIME_TO_EMPTY 0x06
#define SB_AT_RATE_OK 0x07
#define SB_TEMPERATURE 0x08
#define SB_VOLTAGE 0x09
#define SB_CURRENT 0x0a
#define SB_AVERAGE_CURRENT 0x0b
#define SB_MAX_ERROR 0x0c
#define SB_RELATIVE_STATE_OF_CHARGE 0x0d
#define SB_ABSOLUTE_STATE_OF_CHARGE 0x0e
#define SB_REMAINING_CAPACITY 0x0f
#define SB_FULL_CHARGE_CAPACITY 0x10
#define SB_RUN_TIME_TO_EMPTY 0x11
#define SB_AVERAGE_TIME_TO_EMPTY 0x12
#define SB_AVERAGE_TIME_TO_FULL 0x13
#define SB_CHARGING_CURRENT 0x14
#define SB_CHARGING_VOLTAGE 0x15
#define SB_BATTERY_STATUS 0x16
#define SB_CYCLE_COUNT 0x17
#define SB_DESIGN_CAPACITY 0x18
#define SB_DESIGN_VOLTAGE 0x19
#define SB_SPECIFICATION_INFO 0x1a
#define SB_MANUFACTURE_DATE 0x1b
#define SB_SERIAL_NUMBER 0x1c
#define SB_MANUFACTURER_NAME 0x20
#define SB_DEVICE_NAME 0x21
#define SB_DEVICE_CHEMISTRY 0x22
#define SB_MANUFACTURER_DATA 0x23
#define SB_OPTIONAL_MFG_FUNC1 0x3C
#define SB_OPTIONAL_MFG_FUNC2 0x3D
#define SB_OPTIONAL_MFG_FUNC3 0x3E
#define SB_OPTIONAL_MFG_FUNC4 0x3F
/* Extension of smart battery spec, may not be supported on all platforms */
#define SB_PACK_STATUS 0x43
#define SB_ALT_MANUFACTURER_ACCESS 0x44
#define SB_MANUFACTURE_INFO 0x70

/* Battery mode */
#define MODE_INTERNAL_CHARGE_CONTROLLER BIT(0)
#define MODE_PRIMARY_BATTERY_SUPPORT BIT(1)
#define MODE_CONDITION_CYCLE BIT(7)
#define MODE_CHARGE_CONTROLLER_ENABLED BIT(8)
#define MODE_PRIMARY_BATTERY BIT(9)
#define MODE_ALARM BIT(13)
#define MODE_CHARGER BIT(14)
#define MODE_CAPACITY BIT(15)

/* Battery status */
#define STATUS_ERR_CODE_MASK 0xf
#define STATUS_CODE_OK 0
#define STATUS_CODE_BUSY 1
#define STATUS_CODE_RESERVED 2
#define STATUS_CODE_UNSUPPORTED 3
#define STATUS_CODE_ACCESS_DENIED 4
#define STATUS_CODE_OVERUNDERFLOW 5
#define STATUS_CODE_BADSIZE 6
#define STATUS_CODE_UNKNOWN_ERROR 7
#define STATUS_FULLY_DISCHARGED BIT(4)
#define STATUS_FULLY_CHARGED BIT(5)
#define STATUS_DISCHARGING BIT(6)
#define STATUS_INITIALIZED BIT(7)
#define STATUS_REMAINING_TIME_ALARM BIT(8)
#define STATUS_REMAINING_CAPACITY_ALARM BIT(9)
#define STATUS_TERMINATE_DISCHARGE_ALARM BIT(11)
#define STATUS_OVERTEMP_ALARM BIT(12)
#define STATUS_TERMINATE_CHARGE_ALARM BIT(14)
#define STATUS_OVERCHARGED_ALARM BIT(15)

/* Battery Spec Info */
#define BATTERY_SPEC_REVISION_MASK 0x000F
#define BATTERY_SPEC_REVISION_SHIFT 0
#define BATTERY_SPEC_VERSION_MASK 0x00F0
#define BATTERY_SPEC_VERSION_SHIFT 4
#define BATTERY_SPEC_VSCALE_MASK 0x0F00
#define BATTERY_SPEC_VSCALE_SHIFT 8
#define BATTERY_SPEC_IPSCALE_MASK 0xF000
#define BATTERY_SPEC_IPSCALE_SHIFT 12

#define BATTERY_SPEC_VERSION(INFO) \
	((INFO & BATTERY_SPEC_VERSION_MASK) >> BATTERY_SPEC_VERSION_SHIFT)
/* Smart battery version info */
#define BATTERY_SPEC_VER_1_0 1
#define BATTERY_SPEC_VER_1_1 2
#define BATTERY_SPEC_VER_1_1_WITH_PEC 3
/* Smart battery revision info */
#define BATTERY_SPEC_REVISION_1 1

/* Charge mode */
#define CHARGE_FLAG_INHIBIT_CHARGE BIT(0)
#define CHARGE_FLAG_ENABLE_POLLING BIT(1)
#define CHARGE_FLAG_POR_RESET BIT(2)
#define CHARGE_FLAG_RESET_TO_ZERO BIT(3)
/* Charger status */
#define CHARGER_CHARGE_INHIBITED BIT(0)
#define CHARGER_POLLING_ENABLED BIT(1)
#define CHARGER_VOLTAGE_NOTREG BIT(2)
#define CHARGER_CURRENT_NOTREG BIT(3)
#define CHARGER_LEVEL_2 BIT(4)
#define CHARGER_LEVEL_3 BIT(5)
#define CHARGER_CURRENT_OR BIT(6)
#define CHARGER_VOLTAGE_OR BIT(7)
#define CHARGER_RES_OR BIT(8)
#define CHARGER_RES_COLD BIT(9)
#define CHARGER_RES_HOT BIT(10)
#define CHARGER_RES_UR BIT(11)
#define CHARGER_ALARM_INHIBITED BIT(12)
#define CHARGER_POWER_FAIL BIT(13)
#define CHARGER_BATTERY_PRESENT BIT(14)
#define CHARGER_AC_PRESENT BIT(15)
#define CHARGER_BYPASS_MODE BIT(16)
/* Charger specification info */
#define INFO_CHARGER_SPEC(INFO) ((INFO)&0xf)
#define INFO_SELECTOR_SUPPORT(INFO) (((INFO) >> 4) & 1)

/* Manufacturer Access parameters */
#define PARAM_SAFETY_STATUS 0x51
#define PARAM_OPERATION_STATUS 0x54
#define PARAM_FIRMWARE_RUNTIME 0x62
/* Operation status masks -- 6 byte reply */
/* reply[3] */
#define BATTERY_DISCHARGING_DISABLED 0x20
#define BATTERY_CHARGING_DISABLED 0x40

/* Battery manufacture date */
#define MANUFACTURE_DATE_DAY_MASK 0x001F
#define MANUFACTURE_DATE_DAY_SHIFT 0
#define MANUFACTURE_DATE_MONTH_MASK 0x01E0
#define MANUFACTURE_DATE_MONTH_SHIFT 5
#define MANUFACTURE_DATE_YEAR_MASK 0xFE00
#define MANUFACTURE_DATE_YEAR_SHIFT 9
#define MANUFACTURE_DATE_YEAR_OFFSET 1980
#define MANUFACTURE_RUNTIME_SIZE 4

/* Read from battery */
int sb_read(int cmd, int *param);

/**
 * Read null-terminated string from battery
 * @param offset	Battery register to read from
 * @param data		Buffer to hold the string
 * @param len		Length of data buffer
 */
int sb_read_string(int offset, uint8_t *data, int len);

/**
 * Read sized block of data from battery
 * @param offset	Battery register to read from
 * @param data		Buffer to hold read data
 * @param len		Length of data buffer
 */
int sb_read_sized_block(int offset, uint8_t *data, int len);

/* Write to battery */
int sb_write(int cmd, int param);

/**
 * Write block to do battery cutoff
 *
 * @param reg		Battery cutoff register
 * @param val		Battery cutoff data value
 * @param len		Param val data length
 * @return          non-zero if error
 */
int sb_write_block(int reg, const uint8_t *val, int len);

/* Read manufactures access data from the battery */
int sb_read_mfgacc(int cmd, int block, uint8_t *data, int len);

/* Read manufactures access data from the battery */
int sb_read_mfgacc_block(int cmd, int block, uint8_t *data, int len);

#endif /* __CROS_EC_BATTERY_SMART_H */
