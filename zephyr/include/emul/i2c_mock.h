/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_I2C_MOCK_H_
#define ZEPHYR_INCLUDE_EMUL_I2C_MOCK_H_

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>

/**
 * @brief reset the I2C mock.
 *
 * @param emul The mock device to reset.
 */
void i2c_mock_reset(const struct emul *emul);

/**
 * @brief Get the i2c emulator pointer from the top level mock.
 *
 * @param emul The mock device to query
 * @return Pointer to the i2c emulator struct
 */
struct i2c_emul *i2c_mock_to_i2c_emul(const struct emul *emul);

/**
 * @brief Get the I2C address of the mock
 *
 * @param emul The mock device to query
 * @return The address on the I2C bus
 */
uint16_t i2c_mock_get_addr(const struct emul *emul);

/**
 * @brief Returns pointer to i2c_common_emul_data for argument emul
 *
 * @param emul Pointer to i2c_mock emulator
 * @return Pointer to i2c_common_emul_data from argument emul
 */
struct i2c_common_emul_data *
emul_i2c_mock_get_i2c_common_data(const struct emul *emul);

#endif /* ZEPHYR_INCLUDE_EMUL_I2C_MOCK_H_ */
