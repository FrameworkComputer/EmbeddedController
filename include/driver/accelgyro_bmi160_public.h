/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMI160 accelerometer and gyro for Chrome EC */

#ifndef __CROS_EC_DRIVER_ACCELGYRO_BMI160_PUBLIC_H
#define __CROS_EC_DRIVER_ACCELGYRO_BMI160_PUBLIC_H

/*
 * The addr field of motion_sensor support both SPI and I2C:
 * This is defined in include/i2c.h and is no longer an 8bit
 * address. The 7/10 bit address starts at bit 0 and leaves
 * room for a 10 bit address, although we don't currently
 * have any 10 bit peripherals. I2C or SPI is indicated by a
 * more significant bit
 */

/* I2C addresses */
#define BMI160_ADDR0_FLAGS      0x68

extern const struct accelgyro_drv bmi160_drv;

void bmi160_interrupt(enum gpio_signal signal);
int bmi160_get_sensor_temp(int idx, int *temp_ptr);

#ifdef CONFIG_CMD_I2C_STRESS_TEST_ACCEL
extern struct i2c_stress_test_dev bmi160_i2c_stress_test_dev;
#endif

#endif /* __CROS_EC_DRIVER_ACCELGYRO_BMI260_PUBLIC_H */
