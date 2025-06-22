/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implementation of I2C HID for media keys */
#ifndef __CROS_EC_I2C_HID_MEDIAKEYS_H
#define __CROS_EC_I2C_HID_MEDIAKEYS_H

#include "common.h"
#include "stdbool.h"
#include "stdint.h"

/* I2C slave address */
#define I2C_HID_SLAVE_ADDR 0x50

/* 2 bytes for length + 1 byte for report ID */
#define I2C_HID_HEADER_SIZE			3

/* Framework HID fields */
#define I2C_HID_MEDIAKEYS_VENDOR_ID 0x32AC
#define I2C_HID_MEDIAKEYS_PRODUCT_ID 0x0006
#define I2C_HID_MEDIAKEYS_FW_VERSION 0x0001
#define I2C_HID_MEDIAKEYS_HID_DESC_REGISTER	0x0055



/**
 * ALS HID Unit Exponent
 * 0x00 = 1	(Default)
 * 0x0C = 0.0001
 * 0x0D = 0.001
 * 0x0E = 0.01
 * 0x0F = 0.1
 */
#define ALS_HID_UNIT	0x00

#define HID_ALS_MAX 65535
#define HID_ALS_MIN 0
#define HID_ALS_SENSITIVITY 10

/* HID_USAGE_SENSOR_PROPERTY_SENSOR_CONNECTION_TYPE */
#define HID_INTEGRATED			1
#define HID_ATTACHED			2
#define HID_EXTERNAL			3

/* HID_USAGE_SENSOR_PROPERTY_REPORTING_STATE */
#define HID_NO_EVENTS			1
#define HID_ALL_EVENTS			2
#define HID_THRESHOLD_EVENTS		3
#define HID_NO_EVENTS_WAKE		4
#define HID_ALL_EVENTS_WAKE		5
#define HID_THRESHOLD_EVENTS_WAKE	6

/* HID_USAGE_SENSOR_PROPERTY_POWER_STATE */
#define HID_UNDEFINED			1
#define HID_D0_FULL_POWER		2
#define HID_D1_LOW_POWER		3
#define HID_D2_STANDBY_WITH_WAKE 	4
#define HID_D3_SLEEP_WITH_WAKE		5
#define HID_D4_POWER_OFF		6

/* HID_USAGE_SENSOR_STATE */
#define HID_UNKNOWN			1
#define HID_READY			2
#define HID_NOT_AVAILABLE		3
#define HID_NO_DATA			4
#define HID_INITIALIZING		5
#define HID_ACCESS_DENIED		6
#define HID_ERROR			7

/* HID_USAGE_SENSOR_EVENT */
#define HID_UNKNOWN			1
#define HID_STATE_CHANGED		2
#define HID_PROPERTY_CHANGED		3
#define HID_DATA_UPDATED		4
#define HID_POLL_RESPONSE		5
#define HID_CHANGE_SENSITIVITY		6

enum media_key {
    HID_KEY_DISPLAY_BRIGHTNESS_UP,
    HID_KEY_DISPLAY_BRIGHTNESS_DN,
    HID_KEY_AIRPLANE_MODE,
    HID_KEY_DISPLAY_TOGGLE,
    HID_ALS_REPORT_LUX,
    HID_KEY_MAX
};
/*HID_KEY_MAX cannot be > TASK_EVENT_CUSTOM_BIT*/
BUILD_ASSERT(HID_KEY_MAX < 16);

int update_hid_key(enum media_key key, bool pressed);
void set_illuminance_value(uint16_t value);

#endif /* __CROS_EC_I2C_HID_MEDIAKEYS_H */