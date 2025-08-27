/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_APL6012_H
#define __CROS_EC_APL6012_H

#include "i2c.h"
#include "temp_sensor/thermistor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default 0x7E for 8 bit address */
#define APL6012_I2C_ADDR_FLAGS 0x3F

#define APL6012_MV_OFFSET 1000
#define APL6012_MV_STEP 10

/*
 * I2C port and address information for all the board APL6012 sensors should be
 * defined in an array of the following structures, with an enum apl6012_sensor
 * indexing the array. The enum apl6012_sensor shall end with a
 * APL6012_IDX_COUNT defining the maximum number of sensors for the board.
 */
struct apl6012_sensor_t {
	int i2c_port;
	int i2c_addr_flags;
};

extern const struct apl6012_sensor_t apl6012_sensors[];

/*
 * The APL6012 driver only supports a single device instance on the board.
 * Each device supports 15 channel temperature sensor types: ch1, ..., ch15.
 * The apl6012_index selects the temperature sensor type to read.
 */
enum apl6012_index {
	APL6012_IDX_CH1,
	APL6012_IDX_CH2,
	APL6012_IDX_CH3,
	APL6012_IDX_CH4,
	APL6012_IDX_CH5,
	APL6012_IDX_CH6,
	APL6012_IDX_CH7,
	APL6012_IDX_CH8,
	APL6012_IDX_CH9,
	APL6012_IDX_CH10,
	APL6012_IDX_CH11,
	APL6012_IDX_CH12,
	APL6012_IDX_CH13,
	APL6012_IDX_CH14,
	APL6012_IDX_CH15,
	APL6012_IDX_COUNT,
};

#define APL6012_TD1 0x0F
#define APL6012_TD2 0x10
#define APL6012_TD3 0x11
#define APL6012_TD4 0x12
#define APL6012_TD5 0x13
#define APL6012_TD6 0x14
#define APL6012_TD7 0x15
#define APL6012_TD8 0x16
#define APL6012_TD9 0x17
#define APL6012_TD10 0x18
#define APL6012_TD11 0x19
#define APL6012_TD12 0x1A
#define APL6012_TD13 0x1B
#define APL6012_TD14 0x1C
#define APL6012_TD15 0x1D

/**
 * Get the last polled value of a sensor.
 *
 * @param idx Index to read, from temps[APL6012_IDX_COUNT] definition
 *
 * @param temp_k_ptr Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int apl6012_get_val_k(int idx, int *temp_k_ptr);

/*
 * Required for ADC temperature calculation.
 * Non-zephyr devices that using ADC must define this in board layer.
 */
extern const struct thermistor_info apl6012_thermistor_info[];

/**
 * Update the specified channel temperature every HOOK_SECOND.
 *
 * @param idx Index to be updated, from temp_sensors[] definition
 */
void apl6012_update_temperature(int idx);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_APL6012_H */
