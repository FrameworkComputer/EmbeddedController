/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UM_PPM_CHARDEV_H_
#define UM_PPM_CHARDEV_H_

/* Forward declaration. */
struct smbus_driver;
struct pd_driver_config;
struct ucsi_pd_driver;
struct um_ppm_cdev;

/* Set up the um_ppm device to start communicating with kernel. */
int cdev_prepare_um_ppm(const char *um_test_devpath, struct ucsi_pd_driver *pd,
			struct smbus_driver *smbus,
			struct pd_driver_config *config);

#endif /* UM_PPM_CHARDEV_H_ */
