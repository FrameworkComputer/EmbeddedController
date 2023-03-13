/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef HID_OVER_I2C_TOUCHPAD_H
#define HID_OVER_I2C_TOUCHPAD_H

#include "usb_hid_touchpad.h"

/**
 * Add touchpad event into hid-i2c FIFO
 *
 * @param report HID report to add
 */
void hid_i2c_touchpad_add(const struct usb_hid_touchpad_report *report);

#endif
