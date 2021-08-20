/* Copyright 2021 The Chromium OS Authors. All rights reserved.
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

#include <emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

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
 * - call @ref bb_emul_set_read_func and @ref bb_emul_set_write_func to setup
 *   custom handlers for I2C messages
 * - call @ref bb_emul_set_reg and @ref bb_emul_get_reg to set and get value
 *   of BB retimers registers
 * - call bb_emul_set_err_* to change emulator behaviour on inadvisable driver
 *   behaviour
 * - call @ref bb_emul_set_read_fail_reg and @ref bb_emul_set_write_fail_reg
 *   to configure emulator to fail on given register read or write
 */

/**
 * Special register values used in @ref bb_emul_set_read_fail_reg and
 * @ref bb_emul_set_write_fail_reg
 */
#define BB_EMUL_FAIL_ALL_REG	(-1)
#define BB_EMUL_NO_FAIL_REG	(-2)

/**
 * @brief Get pointer to BB retimer emulator using device tree order number.
 *
 * @param ord Device tree order number obtained from DT_DEP_ORD macro
 *
 * @return Pointer to BB retimer emulator
 */
struct i2c_emul *bb_emul_get(int ord);

/**
 * @brief Custom function type that is used as user-defined callback in read
 *        I2C messages handling.
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Address which is now accessed by read command
 * @param data Pointer to custom user data
 *
 * @return 0 on success. Value of @p reg should be set by @ref bb_emul_set_reg
 * @return 1 continue with normal BB retimer emulator handler
 * @return negative on error
 */
typedef int (*bb_emul_read_func)(struct i2c_emul *emul, int reg, void *data);

/**
 * @brief Custom function type that is used as user-defined callback in write
 *        I2C messages handling.
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Address which is now accessed by write command
 * @param val Value which is being written to @p reg
 * @param data Pointer to custom user data
 *
 * @return 0 on success
 * @return 1 continue with normal BB retimer emulator handler
 * @return negative on error
 */
typedef int (*bb_emul_write_func)(struct i2c_emul *emul, int reg, uint32_t val,
				  void *data);

/**
 * @brief Lock access to BB retimer properties. After acquiring lock, user
 *        may change emulator behaviour in multi-thread setup.
 *
 * @param emul Pointer to BB retimer emulator
 * @param timeout Timeout in getting lock
 *
 * @return k_mutex_lock return code
 */
int bb_emul_lock_data(struct i2c_emul *emul, k_timeout_t timeout);

/**
 * @brief Unlock access to BB retimer properties.
 *
 * @param emul Pointer to BB retimer emulator
 *
 * @return k_mutex_unlock return code
 */
int bb_emul_unlock_data(struct i2c_emul *emul);

/**
 * @brief Set write handler for I2C messages. This function is called before
 *        generic handler.
 *
 * @param emul Pointer to BB retimer emulator
 * @param func Pointer to custom function
 * @param data User data passed on call of custom function
 */
void bb_emul_set_write_func(struct i2c_emul *emul, bb_emul_write_func func,
			    void *data);

/**
 * @brief Set read handler for I2C messages. This function is called before
 *        generic handler.
 *
 * @param emul Pointer to BB retimer emulator
 * @param func Pointer to custom function
 * @param data User data passed on call of custom function
 */
void bb_emul_set_read_func(struct i2c_emul *emul, bb_emul_read_func func,
			   void *data);

/**
 * @brief Set value of given register of BB retimer
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Register address which value will be changed
 * @param val New value of the register
 */
void bb_emul_set_reg(struct i2c_emul *emul, int reg, uint32_t val);

/**
 * @brief Get value of given register of BB retimer
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Register address
 *
 * @return Value of the register
 */
uint32_t bb_emul_get_reg(struct i2c_emul *emul, int reg);

/**
 * @brief Setup fail on read of given register of BB retimer
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Register address or one of special values (BB_EMUL_FAIL_ALL_REG,
 *            BB_EMUL_NO_FAIL_REG)
 */
void bb_emul_set_read_fail_reg(struct i2c_emul *emul, int reg);

/**
 * @brief Setup fail on write of given register of BB retimer
 *
 * @param emul Pointer to BB retimer emulator
 * @param reg Register address or one of special values (BB_EMUL_FAIL_ALL_REG,
 *            BB_EMUL_NO_FAIL_REG)
 */
void bb_emul_set_write_fail_reg(struct i2c_emul *emul, int reg);

/**
 * @brief Set if error should be generated when read only register is being
 *        written
 *
 * @param emul Pointer to BB retimer emulator
 * @param set Check for this error
 */
void bb_emul_set_err_on_ro_write(struct i2c_emul *emul, bool set);

/**
 * @brief Set if error should be generated when reserved bits of register are
 *        not set to 0 on write I2C message
 *
 * @param emul Pointer to BB retimer emulator
 * @param set Check for this error
 */
void bb_emul_set_err_on_rsvd_write(struct i2c_emul *emul, bool set);

/**
 * @}
 */

#endif /* __EMUL_BB_RETIMER */
