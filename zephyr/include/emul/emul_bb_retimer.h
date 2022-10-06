/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for BB retimer emulator
 */

#ifndef __EMUL_BB_RETIMER_H
#define __EMUL_BB_RETIMER_H

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

/**
 * @brief BB retimer emulator backend API
 * @defgroup bb_emul BB retimer emulator
 * @{
 *
 * BB retimer emulator supports access to all its registers using I2C messages.
 * It supports not four bytes writes by padding zeros (the same as real
 * device), but show warning in that case.
 * Application may alter emulator state:
 *
 * - define a Device Tree overlay file to set default vendor ID and which
 *   inadvisable driver behaviour should be treated as errors
 * - call @ref bb_emul_set_reg and @ref bb_emul_get_reg to set and get value
 *   of BB retimers registers
 * - call bb_emul_set_err_* to change emulator behaviour on inadvisable driver
 *   behaviour
 * - call functions from emul_common_i2c.h to setup custom handlers for I2C
 *   messages
 */

/**
 * @brief Set value of given register of BB retimer
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Register address which value will be changed
 * @param val New value of the register
 */
void bb_emul_set_reg(const struct emul *emul, int reg, uint32_t val);

/**
 * @brief Get value of given register of BB retimer
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Register address
 *
 * @return Value of the register
 */
uint32_t bb_emul_get_reg(const struct emul *emul, int reg);

/**
 * @brief Set if error should be generated when read only register is being
 *        written
 *
 * @param emul Pointer to BB retimer emulator
 * @param set Check for this error
 */
void bb_emul_set_err_on_ro_write(const struct emul *emul, bool set);

/**
 * @brief Set if error should be generated when reserved bits of register are
 *        not set to 0 on write I2C message
 *
 * @param emul Pointer to BB retimer emulator
 * @param set Check for this error
 */
void bb_emul_set_err_on_rsvd_write(const struct emul *emul, bool set);

/**
 * @brief Returns pointer to i2c_common_emul_data for given emul
 *
 * @param emul Pointer to bb retimer emulator
 * @return Pointer to i2c_common_emul_data for emul argument
 */
struct i2c_common_emul_data *
emul_bb_retimer_get_i2c_common_data(const struct emul *emul);

/**
 * @}
 */

#endif /* __EMUL_BB_RETIMER */
