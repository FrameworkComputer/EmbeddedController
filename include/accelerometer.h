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
int accel_read(const enum accel_id id, int * const x_acc, int * const y_acc,
		int * const z_acc);

/**
 * Initialize accelerometers.
 *
 * @param id Target accelerometer
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int accel_init(const enum accel_id id);

/**
 * Setter and getter methods for the sensor range. The sensor range defines
 * the maximum value that can be returned from accel_read(). As the range
 * increases, the resolution gets worse.
 *
 * @param id Target accelerometer
 * @param range Range (Units are +/- G's for accel, +/- deg/s for gyro)
 * @param rnd Rounding flag. If true, it rounds up to nearest valid value.
 *                Otherwise, it rounds down.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int accel_set_range(const enum accel_id id, const int range, const int rnd);
int accel_get_range(const enum accel_id id, int * const range);


/**
 * Setter and getter methods for the sensor resolution.
 *
 * @param id Target accelerometer
 * @param range Resolution (Units are number of bits)
 * param rnd Rounding flag. If true, it rounds up to nearest valid value.
 *                Otherwise, it rounds down.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int accel_set_resolution(const enum accel_id id, const int res, const int rnd);
int accel_get_resolution(const enum accel_id id, int * const res);

/**
 * Setter and getter methods for the sensor output data range. As the ODR
 * increases, the LPF roll-off frequency also increases.
 *
 * @param id Target accelerometer
 * @param rate Output data rate (units are mHz)
 * @param rnd Rounding flag. If true, it rounds up to nearest valid value.
 *                Otherwise, it rounds down.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int accel_set_datarate(const enum accel_id id, const int rate, const int rnd);
int accel_get_datarate(const enum accel_id id, int * const rate);

#endif /* __CROS_EC_ACCELEROMETER_H */
