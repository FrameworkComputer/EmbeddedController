/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_LIS2DW12_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_LIS2DW12_H_

#include <emul.h>
#include <drivers/i2c_emul.h>

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

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_LIS2DW12_H_ */
