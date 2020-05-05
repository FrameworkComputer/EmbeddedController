/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PI3HDX1204 retimer.
 */

#ifndef __CROS_EC_USB_RETIMER_PI3HDX1204_H
#define __CROS_EC_USB_RETIMER_PI3HDX1204_H

#define PI3HDX1204_I2C_ADDR_FLAGS	0x60

/* Register Offset 2 - Enable */
#define PI3HDX1204_ENABLE_OFFSET	2
#define PI3HDX1204_ENABLE_ALL_CHANNELS	0xF0

/* Enable or disable the PI3HDX1204. */
int pi3hdx1204_enable(const int i2c_port,
		      const uint16_t i2c_addr_flags,
		      const int enable);

#endif /* __CROS_EC_USB_RETIMER_PI3HDX1204_H */
