/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_LIS2DW12_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_LIS2DW12_H_

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>

/**
 * @brief The the i2c emulator pointer from the top level emul.
 *
 * @param emul The emulator to query
 * @return Pointer to the i2c emulator struct
 */
struct i2c_emul *lis2dw12_emul_to_i2c_emul(const struct emul *emul);

/**
 * @brief Reset the state of the lis2dw12 emulator.
 *
 * @param emul The emulator to reset.
 */
void lis2dw12_emul_reset(const struct emul *emul);

/**
 * @brief Set the who-am-i register value.
 *
 * By default the who-am-i register holds LIS2DW12_WHO_AM_I, this function
 * enables overriding that value in order to drive testing.
 *
 * @param emul The emulator to modify.
 * @param who_am_i The new who-am-i register value.
 */
void lis2dw12_emul_set_who_am_i(const struct emul *emul, uint8_t who_am_i);

/**
 * @brief Check the number of times the chip was soft reset.
 *
 * This value is reset by a call to lis2dw12_emul_reset().
 *
 * @param emul The emulator to query
 * @return The number of times that the chip was reset.
 */
uint32_t lis2dw12_emul_get_soft_reset_count(const struct emul *emul);

/**
 * @brief Peeks at the value of a register without doing any I2C transaction.
 *        If the register is unsupported, or `emul` is NULL, this function
 *        asserts.
 *
 * @param emul The emulator to query
 * @param reg The register to access
 * @return The value of the register
 */
uint8_t lis2dw12_emul_peek_reg(struct i2c_emul *emul, int reg);

/**
 * @brief Retrieves the ODR[3:0] bits from CRTL1 register
 *
 * @param emul The emulator to query
 * @return The ODR bits, right-aligned
 */
uint8_t lis2dw12_emul_peek_odr(struct i2c_emul *emul);

/**
 * @brief Retrieves the MODE[1:0] bits from CRTL1 register
 *
 * @param emul The emulator to query
 * @return The MODE bits, right-aligned
 */
uint8_t lis2dw12_emul_peek_mode(struct i2c_emul *emul);

/**
 * @brief Retrieves the LPMODE[1:0] bits from CRTL1 register
 *
 * @param emul The emulator to query
 * @return The LPMODE bits, right-aligned
 */
uint8_t lis2dw12_emul_peek_lpmode(struct i2c_emul *emul);

/**
 * @brief Updates the current 3-axis acceleromter reading and
 *        sets the DRDY (data ready) flag.
 * @param emul Reference to current LIS2DW12 emulator.
 * @param reading array of int X, Y, and Z readings.
 * @return 0 on success, or -EINVAL if readings are out of bounds.
 */
int lis2dw12_emul_set_accel_reading(const struct emul *emul,
				    intv3_t reading);

/**
 * @brief Clears the current accelerometer reading and resets the
 *        DRDY (data ready) flag.
 * @param emul Reference to current LIS2DW12 emulator.
 */
void lis2dw12_emul_clear_accel_reading(const struct emul *emul);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_LIS2DW12_H_ */
