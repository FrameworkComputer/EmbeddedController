/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file defines the EC commands used in mailbox between host and EC.
 *
 * This file is included by both BIOS/OS and EC firmware.
 */

#ifndef __HOST_INTERFACE_EC_COMMAND_H
#define __HOST_INTERFACE_EC_COMMAND_H

#include "cros_ec/include/ec_common.h"


enum EcCommand {
  /*------------------------------------------------------------------------*/
  /* Version and boot information */
  EC_COMMAND_INFO_CMD = 0x00,
  EC_COMMAND_INFO_CMD_MASK = 0xf0,
  EC_COMMAND_INFO_GET_CHIP_ID = 0x01,
  EC_COMMAND_INFO_GET_ACTIVE_FIRMWARE = 0x02,
  EC_COMMAND_INFO_GET_FIRMWARE_VERSION = 0x03,
  EC_COMMAND_INFO_GET_RECOVERY_REASON = 0x04,
  EC_COMMAND_INFO_SET_TRY_B_COUNT = 0x05,
  EC_COMMAND_INFO_GET_TRY_B_COUNT = 0x06,
  EC_COMMAND_INFO_REQUEST_REBOOT = 0x07,
  EC_COMMAND_INFO_GET_VBOOT_INFO = 0x08,
  EC_COMMAND_INFO_RESET_ROLLBACK_INDEX = 0x09,

  /*------------------------------------------------------------------------*/
  /* keyboard (not in 8042 protocol */
  EC_COMMAND_KEYBOARD_CMD = 0x10,
  EC_COMMAND_KEYBOARD_CMD_MASK = 0xf0,
  EC_COMMAND_KEYBOARD_SET_BACKLIGHT = 0x11,
  EC_COMMAND_KEYBOARD_GET_BACKLIGHT = 0x12,
  EC_COMMAND_KEYBOARD_GET_KEY_DOWN_LIST = 0x13,
  EC_COMMAND_KEYBOARD_GET_PWB_HOLD_TIME = 0x14,

  /*------------------------------------------------------------------------*/
  /* Thermal and fan */
  EC_COMMAND_THERMAL_CMD = 0x20,
  EC_COMMAND_THERMAL_CMD_MASK = 0xf0,
  EC_COMMAND_THERMAL_GET_CURRENT_FAN_RPM = 0x21,
  EC_COMMAND_THERMAL_GET_TARGET_FAN_RPM = 0x22,
  EC_COMMAND_THERMAL_SET_TARGET_FAN_RPM = 0x23,
  EC_COMMAND_THERMAL_READ_SENSOR = 0x24,
  EC_COMMAND_THERMAL_SET_ALARM_RANGE = 0x25,
  /* TODO: PECI? */

  /*------------------------------------------------------------------------*/
  /* Power */
  EC_COMMAND_POWER_CMD = 0x30,
  EC_COMMAND_POWER_CMD_MASK = 0xf0,
  EC_COMMAND_POWER_SET_S3_WAKE_REASON = 0x31,
  EC_COMMAND_POWER_GET_S3_WAKE_REASON = 0x32,
  EC_COMMAND_POWER_SET_TARGET_POWER_STATE = 0x33,
  EC_COMMAND_POWER_GET_TARGET_POWER_STATE = 0x34,
  EC_COMMAND_POWER_GET_CURRENT_POWER_STATE = 0x35,

  /*------------------------------------------------------------------------*/
  /* BATTERYTERY */
  EC_COMMAND_BATTERY_CMD = 0x40,
  EC_COMMAND_BATTERY_CMD_MASK = 0xe0,  /* 0x41 ~ 0x5f */
  EC_COMMAND_BATTERY_GET_FLAGS = 0x41,
  EC_COMMAND_BATTERY_GET_REMAIN_CAP_PERCENT = 0x42,
  EC_COMMAND_BATTERY_GET_REMAIN_CAP_MAH = 0x43,
  EC_COMMAND_BATTERY_GET_CURRENT_DRAIN_RATE = 0x44,
  EC_COMMAND_BATTERY_GET_VOLTAGE = 0x45,
  EC_COMMAND_BATTERY_GET_DESIGN_CAP = 0x46,
  EC_COMMAND_BATTERY_GET_DESIGN_MIN_CAP = 0x47,
  EC_COMMAND_BATTERY_GET_CURRENT_CAP = 0x48,
  EC_COMMAND_BATTERY_GET_DESIGN_VOL = 0x49,
  EC_COMMAND_BATTERY_GET_TEMPERATURE = 0x4a,
  EC_COMMAND_BATTERY_GET_TYPE = 0x4b,
  EC_COMMAND_BATTERY_GET_OEM_INFO = 0x4c,
  EC_COMMAND_BATTERY_GET_TIME_REMAIN = 0x4d,
  EC_COMMAND_BATTERY_SET_ENABLE_CHARGE = 0x50,
  EC_COMMAND_BATTERY_SET_ENABLE_AC = 0x51,

  /*------------------------------------------------------------------------*/
  /* Lid */
  EC_COMMAND_LID_CMD = 0x60,
  EC_COMMAND_LID_CMD_MASK = 0xf0,
  EC_COMMAND_LID_GET_FLAGS = 0x41,

  /*------------------------------------------------------------------------*/
  /* Flash */
  EC_COMMAND_FLASH_CMD = 0x70,
  EC_COMMAND_FLASH_CMD_MASK = 0xf0,
  EC_COMMAND_FLASH_GET_INFO = 0x71,
  EC_COMMAND_FLAHS_READ = 0x72,
  EC_COMMAND_FLASH_WRITE = 0x73,
  EC_COMMAND_FLASH_ERASE = 0x74,
  EC_COMMAND_FLASH_SET_ENABLE_WRITE_PROTECT = 0x75,
  EC_COMMAND_FLASH_GET_ENABLE_WRITE_PROTECT = 0x76,
  EC_COMMAND_FLASH_SET_WRITE_PROTECT_RANGE = 0x77,
  EC_COMMAND_FLASH_GET_WRITE_PROTECT_RANGE = 0x78,
  EC_COMMAND_FLASH_GET_WRITE_PROTECT_GPIO = 0x79,
  EC_COMMAND_FLASH_GET_FMAP_OFFSET = 0x7a,

  /*------------------------------------------------------------------------*/
  /* Debug */
  EC_COMMAND_DEBUG_CMD = 0x80,
  EC_COMMAND_DEBUG_CMD_MASK = 0xf0,
  EC_COMMAND_DEBUG_GET_EC_BOOT_REASON = 0x81,
  EC_COMMAND_DEBUG_GET_LAST_CRASH_INFO = 0x82,
  EC_COMMAND_DEBUG_GET_GPIO_VALUE = 0x83,
};


/*
 * To be as portable as possible between EC chips, we employ the follwoing
 * mechanism for mailbox:
 *
 *   - define MB_EC (0xEF) for port 0x66 (ACPI).
 *   - define 2 port ranges for half-duplex channels, i.e.
 *       to_EC: port 0x800-0x9ff
 *       to_host: port 0xa00-0xbff
 *   - the process flow:
 *     - host writes parameters into to_EC range.
 *     - outp(0x62, EC_SET_FAN_RPM);
 *     - outp(0x66, 0xEF);
 *     - EC invokes callback function to handle the corresponding EC command.
 *     - EC writes return parameters into to_host range.
 *     - EC writes return value to port 0x62 so that port 0x66 IBF is set
 *     - host gets the return value and reads parameters from to_host range.
 */


/* When host writes this value to port 0x66 (ACPI command port),
 * the EC firmware would read the EcCommand in port 0x62 and execute
 * the corresponding function.
 */
#define EC_MAILBOX_ACPI_COMMAND 0xEF


/* Mailbox I/O port range: to_EC:   0x800-0x9FF
 *                         to_host: 0xA00-0xBFF
 */
#define EC_MAILBOX_TO_EC_PORT_OFFSET 0x800  /* Host writes. EC reads */
#define EC_MAILBOX_TO_EC_PORT_SIZE 0x200
#define EC_MAILBOX_TO_HOST_PORT_OFFSET 0xA00  /* EC writes. Host reads */
#define EC_MAILBOX_TO_HOST_PORT_SIZE 0x200


/* The meta level of return value from EC command. Every EC command can
 * return extra parameters via to_host range. */
enum EcMailboxError {
  EC_MAILBOX_ERROR_SUCCESS = 0,
  EC_MAILBOX_ERROR_GENERIC = 1,  /* generic error */
  EC_MAILBOX_ERROR_UNIMPLEMENTED = 2,
};

/* The callback function can return a value which will be put at port 0x62. */
typedef enum EcMailboxError (*EcMailboxCallback)(
    uint8_t ec_command,
    uint8_t *to_ec,  /* pointer to input parameter */
    uint8_t *to_host  /* pointer to output buffer */);


/* Registering NULL can remove any registered callback. */
EcError EcMailboxRegisterCallback(EcMailboxCallback callback);


#endif  /* __HOST_INTERFACE_EC_COMMAND_H */
