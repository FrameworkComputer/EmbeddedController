/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ACCELEROMETER_H
#define __CROS_EC_ACCELEROMETER_H

/* Header file for accelerometer drivers. */

/* This array must be defined in board.c. */
extern const int accel_addr[];

/* This enum must be defined in board.h. */
enum accel_id;

/* Number of counts from accelerometer that represents 1G acceleration. */
#define ACCEL_G  1024

/**
 * Read all three accelerations of an accelerometer. Note that all three
 * accelerations come back in counts, where ACCEL_G can be used to convert
 * counts to engineering units.
 *
 * @param id Target accelerometer
 * @param x_acc Pointer to store X-axis acceleration (in counts).
 * @param y_acc Pointer to store Y-axis acceleration (in counts).
 * @param z_acc Pointer to store Z-axis acceleration (in counts).
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int accel_read(enum accel_id id, int *x_acc, int *y_acc, int *z_acc);

/**
 * Initiailze accelerometers.
 *
 * @param id Target accelerometer
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int accel_init(enum accel_id id);

#endif /* __CROS_EC_ACCELEROMETER_H */
