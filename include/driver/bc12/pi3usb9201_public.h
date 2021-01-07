/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PI3USB9201 USB BC 1.2 Charger Detector public definitions */

#ifndef __CROS_EC_DRIVER_BC12_PI3USB9201_PUBLIC_H
#define __CROS_EC_DRIVER_BC12_PI3USB9201_PUBLIC_H

/* I2C address */
#define PI3USB9201_I2C_ADDR_0_FLAGS 0x5C
#define PI3USB9201_I2C_ADDR_1_FLAGS 0x5D
#define PI3USB9201_I2C_ADDR_2_FLAGS 0x5E
#define PI3USB9201_I2C_ADDR_3_FLAGS 0x5F

struct pi3usb9201_config_t {
	const int i2c_port;
	const int i2c_addr_flags;
	const int flags;
};

/* Configuration struct defined at board level */
extern const struct pi3usb9201_config_t pi3usb9201_bc12_chips[];

extern const struct bc12_drv pi3usb9201_drv;

#endif /* __CROS_EC_DRIVER_BC12_PI3USB9201_PUBLIC_H */
