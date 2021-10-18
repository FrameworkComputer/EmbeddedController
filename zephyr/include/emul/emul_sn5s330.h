/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_SN5S330_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_SN5S330_H_

#include <emul.h>
#include <drivers/i2c_emul.h>

/**
 * @brief The i2c emulator pointer from the top level emul.
 *
 * @param emul The emulator to query
 * @return Pointer to the i2c emulator struct
 */
struct i2c_emul *sn5s330_emul_to_i2c_emul(const struct emul *emul);

/**
 * @brief Get the register value without incurring any side-effects
 *
 * @param emul The emulator to query
 * @param reg The register to read
 * @param val Pointer to write the register value to
 * @return 0 on success
 */
int sn5s330_emul_peek_reg(const struct emul *emul, uint32_t reg, uint32_t *val);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_SN5S330_H_ */
