/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for LN9310 emulator
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_LN9310_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_LN9310_H_

#include <zephyr/drivers/emul.h>
#include "driver/ln9310.h"
#include <stdbool.h>

/**
 * @brief Select the current emulator to use.
 *
 * Currently, only a single ln9310 can be instantiated at any given instance due
 * to how the driver was written. Once this restriction is removed, there's
 * still an issue with the board_get_battery_cell_type() function as it doesn't
 * take a device pointer. This function selects the current LN9310 context which
 * will serve the data for that board function.
 *
 * @param emulator The LN9310 emulator to select.
 */
void ln9310_emul_set_context(const struct emul *emulator);

/**
 * @brief Clear all the emulator data.
 *
 * @param emulator The LN9310 emulator to clear.
 */
void ln9310_emul_reset(const struct emul *emulator);

/**
 * @brief Update the emulator's battery cell type.
 *
 * @param emulator The LN9310 emulator to update.
 * @param type The battery type to use.
 */
void ln9310_emul_set_battery_cell_type(const struct emul *emulator,
				       enum battery_cell_type type);

/**
 * @brief Update the emulator's version number.
 *
 * @param emulator The LN9310 emulator to update.
 * @param version The LN9310 chip version number.
 */
void ln9310_emul_set_version(const struct emul *emulator, int version);

/**
 * @brief Update whether or not the LN9310 is currently getting more than 10V.
 *
 * @param emulator The LN9310 emulator to update.
 * @param is_gt_10v Whether or not the chip is currently getting more than 10V.
 */
void ln9310_emul_set_vin_gt_10v(const struct emul *emulator, bool is_gt_10v);

/**
 * @brief Get whether or not the LN9310 is initialized.
 *
 * @param emulator The LN9310 emulator to read.
 *
 * @return true if the LN9310 was correctly initialized.
 */
bool ln9310_emul_is_init(const struct emul *emulator);

/**
 * @brief Get the I2C emulator struct
 *
 * This is generally coupled with calls to i2c_common_emul_* functions.
 *
 * @param emulator The emulator to look-up
 * @return Pointer to the I2C emulator struct
 */
struct i2c_emul *ln9310_emul_get_i2c_emul(const struct emul *emulator);

/**
 * @brief Returns pointer to i2c_common_emul_data for argument emul
 *
 * @param emul Pointer to LN9310 emulator
 * @return Pointer to i2c_common_emul_data from argument emul
 */
struct i2c_common_emul_data *
emul_ln9310_get_i2c_common_data(const struct emul *emul);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_LN9310_H_ */
