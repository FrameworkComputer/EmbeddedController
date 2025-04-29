/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_I2C_HID_DEVICE_H
#define __CROS_EC_I2C_HID_DEVICE_H

#include "util.h"

/*
 * See hid usage tables for consumer page
 * https://www.usb.org/hid
 */
#define BUTTON_ID_BRIGHTNESS_INCREMENT 0x006F
#define BUTTON_ID_BRIGHTNESS_DECREMENT 0x0070

void hid_consumer(uint16_t  id, bool pressed);
void hid_airplane(bool pressed);

int hid_target_register(const struct device *dev);

int hid_target_unregister(const struct device *dev);

#endif /* __CROS_EC_I2C_HID_DEVICE_H */
