/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_ISL923X_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_ISL923X_H_

#include <device.h>
#include <drivers/i2c_emul.h>
#include <emul.h>

/**
 * @brief Get the emulator's parent bus device
 *
 * @param emulator The emulator to look-up
 * @return Pointer to the bus connecting to the emulator
 */
const struct device *isl923x_emul_get_parent(const struct emul *emulator);

/**
 * @brief Get the I2C emulator struct
 *
 * This is generally coupled with calls to i2c_common_emul_* functions.
 *
 * @param emulator The emulator to look-up
 * @return Pointer to the I2C emulator struct
 */
struct i2c_emul *isl923x_emul_get_i2c_emul(const struct emul *emulator);

/**
 * @brief Reset all registers
 *
 * @param emulator The emulator to modify
 */
void isl923x_emul_reset(const struct emul *emulator);

/**
 * @brief Set the manufacturer ID
 *
 * @param emulator The emulator to modify
 * @param manufacturer_id The new manufacturer ID
 */
void isl923x_emul_set_manufacturer_id(const struct emul *emulator,
				      uint16_t manufacturer_id);

/**
 * @brief Set the device ID
 *
 * @param emulator The emulator to modify
 * @param device_id The new device ID
 */
void isl923x_emul_set_device_id(const struct emul *emulator,
				uint16_t device_id);

/**
 * @brief Check whether or not learn mode is enabled
 *
 * @param emulator The emulator to probe
 * @return True if the emulator is in learn mode
 */
bool isl923x_emul_is_learn_mode_enabled(const struct emul *emulator);

/**
 * @brief Set the emulator's learn mode manually without affecting the driver
 *
 * @param emulator The emulator to modify
 * @param enabled Whether or not learn mode should be enabled
 */
void isl923x_emul_set_learn_mode_enabled(const struct emul *emulator,
					 bool enabled);

/**
 * @brief Set the emulator's ADC vbus register
 *
 * @param emulator The emulator to modify
 * @param value The new ADC register value
 */
void isl923x_emul_set_adc_vbus(const struct emul *emulator,
			       uint16_t value);

/**
 * @brief Set the state of the ACOK pin, which is reflected in the INFO2
 *        register
 *
 * @param value If 1, AC adapter is present. If 0, no adapter is present
 */
void raa489000_emul_set_acok_pin(const struct emul *emulator, uint16_t value);

/**
 * @brief Peek at a register value. This function will assert if the requested
 *        register does is unimplemented.
 *
 * @param emulator Reference to the I2C emulator being used
 * @param reg The address of the register to query
 * @return The 16-bit value of the register
 */
uint16_t isl923x_emul_peek_reg(struct i2c_emul *i2c_emul, int reg);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_ISL923X_H_ */
