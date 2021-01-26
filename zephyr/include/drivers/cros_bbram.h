/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CROS_BBRAM_H_
#define ZEPHYR_INCLUDE_DRIVERS_CROS_BBRAM_H_

#include <device.h>

/**
 * Check if "Invalid Battery-Backed RAM". This may occur as a result to low
 * voltage at the VBAT pin.
 *
 * @return 0 if the Battery-Backed RAM data is valid.
 */
typedef int (*cros_bbram_ibbr)(const struct device *dev);

/**
 * Reset the IBBR status (calling cros_bbram_ibbr will return 0 after this).
 *
 * @return 0 after reset is complete.
 * @see cros_bbram_ibbr
 */
typedef int (*cros_bbram_reset_ibbr)(const struct device *dev);

/**
 * Check for V SBY power failure. This will return an error if the V SBY power
 * domain is turned on after it was off.
 *
 * @return 0 if V SBY power domain is in normal operation.
 */
typedef int (*cros_bbram_vsby)(const struct device *dev);

/**
 * Reset the V SBY status (calling cros_bbram_vsby will return 0 after this).
 *
 * @return 0 after reset is complete.
 * @see cros_bbram_vsby
 */
typedef int (*cros_bbram_reset_vsby)(const struct device *dev);

/**
 * Check for V CC1 power failure. This will return an error if the V CC1 power
 * domain is turned on after it was off.
 *
 * @return 0 if the V CC1 power domain is in normal operation.
 */
typedef int (*cros_bbram_vcc1)(const struct device *dev);

/**
 * Reset the V CC1 status (calling cros_bbram_vcc1 will return 0 after this).
 *
 * @return 0 after reset is complete.
 * @see cros_bbram_vcc1
 */
typedef int (*cros_bbram_reset_vcc1)(const struct device *dev);

typedef int (*cros_bbram_read)(const struct device *dev, int offset, int size,
			       char *data);

typedef int (*cros_bbram_write)(const struct device *dev, int offset, int size,
				char *data);

__subsystem struct cros_bbram_driver_api {
	cros_bbram_ibbr ibbr;
	cros_bbram_reset_ibbr reset_ibbr;
	cros_bbram_vsby vsby;
	cros_bbram_reset_vsby reset_vsby;
	cros_bbram_vcc1 vcc1;
	cros_bbram_reset_vcc1 reset_vcc1;
	cros_bbram_read read;
	cros_bbram_write write;
};

#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_BBRAM_H_ */
