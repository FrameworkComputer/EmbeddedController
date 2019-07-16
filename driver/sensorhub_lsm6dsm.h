/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * LSM6DSM Sensor Hub driver to enable interfacing with external sensors
 * like magnetometer for Chrome EC
 */

#ifndef __CROS_EC_SENSORHUB_LSM6DSM_H
#define __CROS_EC_SENSORHUB_LSM6DSM_H

#include "common.h"
#include "motion_sense.h"

/**
 * Configure the register of an external sensor that is attached to sensor
 * hub with a specific value.
 *
 * @param s Pointer to external motion sensor's data structure.
 * @param slv_addr I2C Slave Address of the external sensor.
 * @param reg Register Address to write within the external sensor.
 * @param val Value to be written into the external sensor register.
 * @return EC_SUCCESS on success, EC error codes on failure.
 */
int sensorhub_config_ext_reg(const struct motion_sensor_t *s,
			     const uint16_t slv_addr_flags,
			     uint8_t reg, uint8_t val);

/**
 * Configure the sensor hub to read data from a specific register of an
 * external sensor that is attached to it.
 *
 * @param s Pointer to external motion sensor's data structure.
 * @param slv_addr I2C Slave Address of the external sensor.
 * @param reg Register Address to read from the external sensor.
 * @param len Length of data to be read.
 * @return EC_SUCCESS on success, EC error codes on failure.
 */
int sensorhub_config_slv0_read(const struct motion_sensor_t *s,
			       const uint16_t slv_addr_flags,
			       uint8_t reg, int len);

/**
 * Reads the data from the register bank that is associated with the slave0
 * of the sensor hub.
 *
 * @param s Pointer to external motion sensor's data structure.
 * @param raw Vector to hold the data from the external sensor.
 * @return EC_SUCCESS on success, EC error codes on failure.
 */
int sensorhub_slv0_data_read(const struct motion_sensor_t *s, uint8_t *raw);

/**
 * Check the identity of the external sensor and then reset the external
 * sensor that is attached to the sensor hub.
 *
 * @param s Pointer to external motion sensor's data structure.
 * @param slv_addr I2C Slave Address of the external sensor.
 * @param whoami_reg Register address to identify the sensor.
 * @param whoami_val Expected value to be read from the whoami_reg.
 * @param rst_reg Register address to reset the external sensor.
 * @param rst_val Value to be written to the reset register.
 * @return EC_SUCCESS on success, EC error codes on failure.
 */
int sensorhub_check_and_rst(const struct motion_sensor_t *s,
			    const uint16_t slv_addr_flags,
			    uint8_t whoami_reg, uint8_t whoami_val,
			    uint8_t rst_reg, uint8_t rst_val);
#endif /* __CROS_EC_SENSORHUB_LSM6DSM_H */
