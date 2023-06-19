/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for ANX7452 retimer emulator
 */

#ifndef __EMUL_ANX7452_H
#define __EMUL_ANX7452_H

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

/** Types of "hidden" I2C devices */
enum anx7452_emul_port {
	TOP_EMUL_PORT,
	CTLTOP_EMUL_PORT,
};

/**
 * @brief ANX7452 retimer emulator backend API
 * @defgroup anx7452_emul ANX7452 retimer emulator
 * @{
 *
 * ANX7452 retimer emulator supports access to all its registers using I2C
 * messages. Application may alter emulator state:
 *
 * - call @ref anx7452_emul_set_reg and @ref anx7452_emul_get_reg to set and get
 * value of ANX7452 retimers registers
 * - call anx7452_emul_set_err_* to change emulator behaviour on inadvisable
 * driver behaviour
 * - call functions from emul_common_i2c.h to setup custom handlers for I2C
 *   messages
 */

/**
 * @brief Set value of given register of ANX7452 retimer
 *
 * @param emul Pointer to ANX7452 retimer emulator
 * @param reg Register address which value will be changed
 * @param val New value of the register
 */
void anx7452_emul_set_reg(const struct emul *emul, int reg, uint8_t val);

/**
 * @brief Get value of given register of ANX7452 retimer
 *
 * @param emul Pointer to ANX7452 retimer emulator
 * @param reg Register address
 *
 * @return Value of the register
 */
uint8_t anx7452_emul_get_reg(const struct emul *emul, int reg);

/**
 * @brief Reset the anx7452 emulator
 *
 * @param emul The emulator to reset
 */
void anx7452_emul_reset(const struct emul *emul);

/**
 * @brief Returns pointer to i2c_common_emul_data for given emul
 *
 * @param emul Pointer to anx7452 retimer emulator
 * @return Pointer to i2c_common_emul_data for emul argument
 */
struct i2c_common_emul_data *
emul_anx7452_get_i2c_common_data(const struct emul *emul,
				 enum anx7452_emul_port port);

/**
 * @}
 */

#endif /* __EMUL_ANX7452 */
