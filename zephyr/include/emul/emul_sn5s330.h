/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_SN5S330_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_SN5S330_H_

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>

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
 */
void sn5s330_emul_peek_reg(const struct emul *emul, uint32_t reg, uint8_t *val);

/**
 * @brief Reset the sn5s330 emulator
 *
 * @param emul The emulator to reset
 */
void sn5s330_emul_reset(const struct emul *emul);

/**
 * @brief Emulate vbus overcurrent clamping condition.
 *
 * @param emul The sn5s330 chip emulator.
 */
void sn5s330_emul_make_vbus_overcurrent(const struct emul *emul);

/**
 * @brief Emulate interrupt pin assertion.
 *
 * @param emul The sn5s330 chip emulator.
 */
void sn5s330_emul_assert_interrupt(const struct emul *emul);

/**
 * @brief Emulate interrupt pin de-assertion.
 *
 * @param emul The sn5s330 chip emulator.
 */
void sn5s330_emul_deassert_interrupt(const struct emul *emul);

/**
 * @brief Emulate vbus voltage is below min 0.6V.
 *
 * @param emul The sn5s330 chip emulator.
 */
void sn5s330_emul_lower_vbus_below_minv(const struct emul *emul);

/**
 * @brief Returns pointer to i2c_common_emul_data for argument emul
 *
 * @param emul Pointer to SN5S330 emulator
 * @return Pointer to i2c_common_emul_data from argument emul
 */
struct i2c_common_emul_data *
emul_sn5s330_get_i2c_common_data(const struct emul *emul);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_SN5S330_H_ */
