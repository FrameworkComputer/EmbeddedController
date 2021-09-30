/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_ISL923X_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_ISL923X_H_

#include <emul.h>
#include <drivers/i2c_emul.h>

/**
 * @brief Get the I2C emulator struct
 *
 * This is generally coupled with calls to i2c_common_emul_* functions.
 *
 * @param emulator The emulator to look-up
 * @return Pointer to the I2C emulator struct
 */
struct i2c_emul *isl923x_emul_get_i2c_emul(const struct emul *emulator);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_ISL923X_H_ */
