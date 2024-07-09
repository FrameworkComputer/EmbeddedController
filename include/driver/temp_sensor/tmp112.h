/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_TMP112_H
#define __CROS_EC_TMP112_H

#include "i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TMP112_I2C_ADDR_FLAGS0 (0x48 | I2C_FLAG_BIG_ENDIAN)
#define TMP112_I2C_ADDR_FLAGS1 (0x49 | I2C_FLAG_BIG_ENDIAN)
#define TMP112_I2C_ADDR_FLAGS2 (0x4A | I2C_FLAG_BIG_ENDIAN)
#define TMP112_I2C_ADDR_FLAGS3 (0x4B | I2C_FLAG_BIG_ENDIAN)

#define TMP112_REG_TEMP 0x00
#define TMP112_REG_CONF 0x01
#define TMP112_REG_HYST 0x02
#define TMP112_REG_MAX 0x03

/*
 * I2C port and address information for all the board TMP112 sensors should be
 * defined in an array of the following structures, with an enum tmp112_sensor
 * indexing the array.  The enum tmp112_sensor shall end with a TMP112_COUNT
 * defining the maximum number of sensors for the board.
 */

struct tmp112_sensor_t {
	int i2c_port;
	int i2c_addr_flags;
};

extern const struct tmp112_sensor_t tmp112_sensors[];

/**
 * Get the last polled value of a sensor.
 *
 * @param idx		Index to read, from board's enum tmp112_sensor
 *			definition
 *
 * @param temp_k_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int tmp112_get_val_k(int idx, int *temp_k_ptr);

/**
 * Get the last polled value of a sensor.
 *
 * @param idx		Index to read, from board's enum tmp112_sensor
 *			definition
 *
 * @param temp_mk_ptr	Destination for temperature in mK.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int tmp112_get_val_mk(int idx, int *temp_mk_ptr);

/**
 * Init the sensors.  Note, this will run automatically on HOOK_INIT, but is
 * made available for boards which may not always power the sensor in all
 * states.
 */
void tmp112_init(void);

#ifdef CONFIG_ZEPHYR
void tmp112_update_temperature(int idx);
#endif /* CONFIG_ZEPHYR */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_TMP112_H */
