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


enum media_key {
    HID_KEY_DISPLAY_BRIGHTNESS_UP,
    HID_KEY_DISPLAY_BRIGHTNESS_DN,
    HID_KEY_AIRPLANE_MODE,
    HID_KEY_KEYBOARD_BACKLIGHT,
    HID_KEY_MAX
};
/*HID_KEY_MAX cannot be > TASK_EVENT_CUSTOM_BIT*/
BUILD_ASSERT(HID_KEY_MAX < 16);

int update_hid_key(enum media_key key, bool pressed);
void kblight_update_hid(uint8_t percent);

#endif /* __CROS_EC_I2C_HID_MEDIAKEYS_H */