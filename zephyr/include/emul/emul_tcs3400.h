/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for TCS3400 emulator
 */

#ifndef __EMUL_TCS3400_H
#define __EMUL_TCS3400_H

#include <emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

/**
 * @brief TCS3400 emulator backend API
 * @defgroup tcs_emul TCS3400 emulator
 * @{
 *
 * TCS3400 emulator supports responses to all write and read I2C messages.
 * Light sensor data registers are obtained from internal emulator state, gain
 * and acquisition time. Application may alter emulator state:
 *
 * - define a devicetree overlay file to set which inadvisable driver behaviour
 *   should be treated as error and emulated device ID and revision
 * - call @ref tcs_emul_set_read_func and @ref tcs_emul_set_write_func to setup
 *   custom handlers for I2C messages
 * - call @ref tcs_emul_set_reg and @ref tcs_emul_get_reg to set and get value
 *   of TCS3400 registers
 * - call @ref tcs_emul_set_val and @ref tcs_emul_set_val to set and get
 *   light sensor value
 * - call tcs_emul_set_err_* to change emulator behaviour on inadvisable driver
 *   behaviour
 * - call @ref tcs_emul_set_read_fail_reg and @ref tcs_emul_set_write_fail_reg
 *   to configure emulator to fail on given register read or write
 */

/**
 * Maximum number of integration cycles (when ATIME is zero). Value read from
 * sensor is proportional to number of integration cycles, e.g. with constant
 * light, value obtainded with 128 cycles will be two times smaller than value
 * obtained with 256 cycles.
 */
#define TCS_EMUL_MAX_CYCLES	256
/**
 * Maximum gain supported by TCS3400. Value read from sensor is multiplied by
 * gain selected in CONTROL register.
 */
#define TCS_EMUL_MAX_GAIN	64

/**
 * Emulator units are value returned with gain x64 and 256 integration cycles.
 * Max value is 1024 returned when gain is x1 and 1 integration cycle. Max value
 * represented in emulator units is 1024 * 64 * 256
 */
#define TCS_EMUL_MAX_VALUE	(1024 * TCS_EMUL_MAX_GAIN * TCS_EMUL_MAX_CYCLES)

/** Axis argument used in @ref tcs_emul_set_val @ref tcs_emul_get_val */
enum tcs_emul_axis {
	TCS_EMUL_R,
	TCS_EMUL_G,
	TCS_EMUL_B,
	TCS_EMUL_C,
	TCS_EMUL_IR,
};

/**
 * Emulator saves only those registers in memory. IR select is stored sparately
 * and other registers are write only.
 */
#define TCS_EMUL_FIRST_REG	TCS_I2C_ENABLE
#define TCS_EMUL_LAST_REG	TCS_I2C_BDATAH
#define TCS_EMUL_REG_COUNT	(TCS_EMUL_LAST_REG - TCS_EMUL_FIRST_REG + 1)

/**
 * Special register values used in @ref tcs_emul_set_read_fail_reg and
 * @ref tcs_emul_set_write_fail_reg
 */
#define TCS_EMUL_FAIL_ALL_REG	(-1)
#define TCS_EMUL_NO_FAIL_REG	(-2)

/**
 * @brief Get pointer to TCS3400 emulator using device tree order number.
 *
 * @param ord Device tree order number obtained from DT_DEP_ORD macro
 *
 * @return Pointer to TCS3400 emulator
 */
struct i2c_emul *tcs_emul_get(int ord);

/**
 * @brief Custom function type that is used as user-defined callback in read
 *        I2C messages handling.
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg Address which is now accessed by read command
 * @param data Pointer to custom user data
 *
 * @return 0 on success. Value of @p reg should be set by @ref tcs_emul_set_reg
 * @return 1 continue with normal TCS3400 emulator handler
 * @return negative on error
 */
typedef int (*tcs_emul_read_func)(struct i2c_emul *emul, int reg, void *data);

/**
 * @brief Custom function type that is used as user-defined callback in write
 *        I2C messages handling.
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg Address which is now accessed by write command
 * @param val Value which is being written to @p reg
 * @param data Pointer to custom user data
 *
 * @return 0 on success
 * @return 1 continue with normal TCS3400 emulator handler
 * @return negative on error
 */
typedef int (*tcs_emul_write_func)(struct i2c_emul *emul, int reg, uint8_t val,
				   void *data);

/**
 * @brief Lock access to TCS3400 properties. After acquiring lock, user
 *        may change emulator behaviour in multi-thread setup.
 *
 * @param emul Pointer to TCS3400 emulator
 * @param timeout Timeout in getting lock
 *
 * @return k_mutex_lock return code
 */
int tcs_emul_lock_data(struct i2c_emul *emul, k_timeout_t timeout);

/**
 * @brief Unlock access to TCS3400 properties.
 *
 * @param emul Pointer to TCS3400 emulator
 *
 * @return k_mutex_unlock return code
 */
int tcs_emul_unlock_data(struct i2c_emul *emul);

/**
 * @brief Set write handler for I2C messages. This function is called before
 *        generic handler.
 *
 * @param emul Pointer to TCS3400 emulator
 * @param func Pointer to custom function
 * @param data User data passed on call of custom function
 */
void tcs_emul_set_write_func(struct i2c_emul *emul, tcs_emul_write_func func,
			     void *data);

/**
 * @brief Set read handler for I2C messages. This function is called before
 *        generic handler.
 *
 * @param emul Pointer to TCS3400 emulator
 * @param func Pointer to custom function
 * @param data User data passed on call of custom function
 */
void tcs_emul_set_read_func(struct i2c_emul *emul, tcs_emul_read_func func,
			    void *data);

/**
 * @brief Set value of given register of TCS3400
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg Register address which value will be changed
 * @param val New value of the register
 */
void tcs_emul_set_reg(struct i2c_emul *emul, int reg, uint8_t val);

/**
 * @brief Get value of given register of TCS3400
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg Register address
 *
 * @return Value of the register
 */
uint8_t tcs_emul_get_reg(struct i2c_emul *emul, int reg);

/**
 * @brief Setup fail on read of given register of TCS3400
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg Register address or one of special values (TCS_EMUL_FAIL_ALL_REG,
 *            TCS_EMUL_NO_FAIL_REG)
 */
void tcs_emul_set_read_fail_reg(struct i2c_emul *emul, int reg);

/**
 * @brief Setup fail on write of given register of TCS3400
 *
 * @param emul Pointer to TCS3400 emulator
 * @param reg Register address or one of special values (TCS_EMUL_FAIL_ALL_REG,
 *            TCS_EMUL_NO_FAIL_REG)
 */
void tcs_emul_set_write_fail_reg(struct i2c_emul *emul, int reg);

/**
 * @brief Get internal value of light sensor for given axis
 *
 * @param emul Pointer to TCS3400 emulator
 * @param axis Axis to access
 *
 * @return Value of given axis with gain x64 and 256 integration cycles
 */
int tcs_emul_get_val(struct i2c_emul *emul, enum tcs_emul_axis axis);

/**
 * @brief Set internal value of light sensor for given axis
 *
 * @param emul Pointer to TCS3400 emulator
 * @param axis Axis to access
 * @param val New value of light sensor for given axis with gain x64 and
 *            256 integration cycles
 */
void tcs_emul_set_val(struct i2c_emul *emul, enum tcs_emul_axis axis, int val);

/**
 * @brief Set if error should be generated when read only register is being
 *        written
 *
 * @param emul Pointer to TCS3400 emulator
 * @param set Check for this error
 */
void tcs_emul_set_err_on_ro_write(struct i2c_emul *emul, bool set);

/**
 * @brief Set if error should be generated when reserved bits of register are
 *        not set to 0 on write I2C message
 *
 * @param emul Pointer to TCS3400 emulator
 * @param set Check for this error
 */
void tcs_emul_set_err_on_rsvd_write(struct i2c_emul *emul, bool set);

/**
 * @brief Set if error should be generated when MSB register is accessed before
 *        LSB register
 *
 * @param emul Pointer to TCS3400 emulator
 * @param set Check for this error
 */
void tcs_emul_set_err_on_msb_first(struct i2c_emul *emul, bool set);

/**
 * @}
 */

#endif /* __EMUL_TCS3400_H */
