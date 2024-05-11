/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * General definitions for I2C-HID
 *
 * For a complete reference, please see the following docs on
 * https://docs.microsoft.com/
 *
 * 1. hid-over-i2c-protocol-spec-v1-0.docx
 */
#ifndef __CROS_EC_I2C_HID_H
#define __CROS_EC_I2C_HID_H

#include "common.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * I2C-HID registers
 *
 * Except for I2C_HID_HID_DESC_REGISTER, fields in this section can be chosen
 * freely so we just picked something that we are happy with.
 *
 * I2C_HID_HID_DESC_REGISTER is defined in the ACPI table so please make sure
 * you have put in the same value there.
 */
#define I2C_HID_HID_DESC_REGISTER 0x0001
#define I2C_HID_REPORT_DESC_REGISTER 0x1000
#define I2C_HID_INPUT_REPORT_REGISTER 0x2000
#define I2C_HID_COMMAND_REGISTER 0x3000
#define I2C_HID_DATA_REGISTER 0x3000

/* I2C-HID commands */
#define I2C_HID_CMD_RESET 0x01
#define I2C_HID_CMD_GET_REPORT 0x02
#define I2C_HID_CMD_SET_REPORT 0x03
#define I2C_HID_CMD_GET_IDLE 0x04
#define I2C_HID_CMD_SET_IDLE 0x05
#define I2C_HID_CMD_GET_PROTOCOL 0x06
#define I2C_HID_CMD_SET_PROTOCOL 0x07
#define I2C_HID_CMD_SET_POWER 0x08

/* Common HID fields */
#define I2C_HID_DESC_LENGTH sizeof(struct i2c_hid_descriptor)
#define I2C_HID_BCD_VERSION 0x0100

/* I2C-HID HID descriptor */
struct __packed i2c_hid_descriptor {
	uint16_t wHIDDescLength;
	uint16_t bcdVersion;
	uint16_t wReportDescLength;
	uint16_t wReportDescRegister;
	uint16_t wInputRegister;
	uint16_t wMaxInputLength;
	uint16_t wOutputRegister;
	uint16_t wMaxOutputLength;
	uint16_t wCommandRegister;
	uint16_t wDataRegister;
	uint16_t wVendorID;
	uint16_t wProductID;
	uint16_t wVersionID;
	uint32_t reserved;
};

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_I2C_HID_H */
