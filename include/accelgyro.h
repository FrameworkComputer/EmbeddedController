/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ACCELGYRO_H
#define __CROS_EC_ACCELGYRO_H

/* Header file for accelerometer / gyro drivers. */

/* Number of counts from accelerometer that represents 1G acceleration. */
#define ACCEL_G  1024

enum accelgyro_chip_t {
	CHIP_TEST,
	CHIP_KXCJ9,
	CHIP_LSM6DS0,
};

enum accelgyro_sensor_t {
	SENSOR_ACCELEROMETER,
	SENSOR_GYRO,
};

struct accelgyro_info {
	enum accelgyro_chip_t chip_type;
	enum accelgyro_sensor_t sensor_type;

	/**
	 * Initialize accelerometers.
	 * @param drv_data Pointer to sensor data.
	 * @i2c_addr i2c slave device address
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*init)(void *drv_data,
		    int i2c_addr);

	/**
	 * Read all three accelerations of an accelerometer. Note that all
	 * three accelerations come back in counts, where ACCEL_G can be used
	 * to convert counts to engineering units.
	 * @param drv_data Pointer to sensor data.
	 * @param x_acc Pointer to store X-axis acceleration (in counts).
	 * @param y_acc Pointer to store Y-axis acceleration (in counts).
	 * @param z_acc Pointer to store Z-axis acceleration (in counts).
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*read)(void *drv_data,
		    int * const x_acc,
		    int * const y_acc,
		    int * const z_acc);

	/**
	 * Setter and getter methods for the sensor range. The sensor range
	 * defines the maximum value that can be returned from read(). As the
	 * range increases, the resolution gets worse.
	 * @param drv_data Pointer to sensor data.
	 * @param range Range (Units are +/- G's for accel, +/- deg/s for gyro)
	 * @param rnd Rounding flag. If true, it rounds up to nearest valid
	 * value. Otherwise, it rounds down.
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*set_range)(void *drv_data,
			 const int range,
			 const int rnd);
	int (*get_range)(void *drv_data,
			 int * const range);

	/**
	 * Setter and getter methods for the sensor resolution.
	 * @param drv_data Pointer to sensor data.
	 * @param range Resolution (Units are number of bits)
	 * param rnd Rounding flag. If true, it rounds up to nearest valid
	 * value. Otherwise, it rounds down.
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*set_resolution)(void *drv_data,
			      const int res,
			      const int rnd);
	int (*get_resolution)(void *drv_data,
			      int * const res);

	/**
	 * Setter and getter methods for the sensor output data range. As the
	 * ODR increases, the LPF roll-off frequency also increases.
	 * @param drv_data Pointer to sensor data.
	 * @param rate Output data rate (units are mHz)
	 * @param rnd Rounding flag. If true, it rounds up to nearest valid
	 * value. Otherwise, it rounds down.
	 * @return EC_SUCCESS if successful, non-zero if error.
	 */
	int (*set_datarate)(void *drv_data,
			    const int rate,
			    const int rnd);
	int (*get_datarate)(void *drv_data,
			    int * const rate);

#ifdef CONFIG_ACCEL_INTERRUPTS
	/**
	 * Setup a one-time accel interrupt. If the threshold is low enough, the
	 * interrupt may trigger due simply to noise and not any real motion.
	 * If the threshold is 0, the interrupt will fire immediately.
	 * @param drv_data Pointer to sensor data.
	 * @param threshold Threshold for interrupt in units of counts.
	 */
	int (*set_interrupt)(void *drv_data,
			     unsigned int threshold);
#endif
};

#endif /* __CROS_EC_ACCELGYRO_H */
