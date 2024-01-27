/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UM_PPM_CHARDEV_H_
#define UM_PPM_CHARDEV_H_

// Forward declaration.
struct smbus_driver;
struct pd_driver_config;
struct ucsi_pd_driver;
struct um_ppm_cdev;

// Handle sigterm and sigkill.
void um_ppm_handle_signal(struct um_ppm_cdev *cdev, int signal);

// Initialize the um_ppm chardev.
struct um_ppm_cdev *um_ppm_cdev_open(char *devpath, struct ucsi_pd_driver *pd,
				     struct smbus_driver *smbus,
				     struct pd_driver_config *driver_config);

// Loop handle the cdev communication and interrupts.
void um_ppm_cdev_mainloop(struct um_ppm_cdev *cdev);

#endif // UM_PPM_CHARDEV_H_
