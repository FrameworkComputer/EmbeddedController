/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for BMA255 emulator
 */

#ifndef __EMUL_BMA255_H
#define __EMUL_BMA255_H

#include <emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

/**
 * @brief BMA255 emulator backend API
 * @defgroup bma_emul BMA255 emulator
 * @{
 *
 * BMA255 emulator supports responses to all write and read I2C messages.
 * Accelerometer registers are obtained from internal emulator state, range
 * register and offset. Only fast compensation is supported by default handler.
 * Registers backed in NVM are fully supported (GP0, GP1, offset). For proper
 * support for interrupts and FIFO, user needs to use custom handlers.
 * Application may alter emulator state:
 *
 * - define a Device Tree overlay file to set default NVM content, default
 *   static accelerometer value and which inadvisable driver behaviour should
 *   be treated as errors
 * - call @ref bma_emul_set_read_func and @ref bma_emul_set_write_func to setup
 *   custom handlers for I2C messages
 * - call @ref bma_emul_set_reg and @ref bma_emul_get_reg to set and get value
 *   of BMA255 registers
 * - call @ref bma_emul_set_off and @ref bma_emul_set_off to set and get
 *   internal offset value
 * - call @ref bma_emul_set_acc and @ref bma_emul_set_acc to set and get
 *   accelerometer value
 * - call bma_emul_set_err_* to change emulator behaviour on inadvisable driver
 *   behaviour
 * - call @ref bma_emul_set_read_fail_reg and @ref bma_emul_set_write_fail_reg
 *   to configure emulator to fail on given register read or write
 */

/**
 * Axis argument used in @ref bma_emul_set_acc @ref bma_emul_get_acc
 * @ref bma_emul_set_off and @ref bma_emul_get_off
 */
#define BMA_EMUL_AXIS_X		0
#define BMA_EMUL_AXIS_Y		1
#define BMA_EMUL_AXIS_Z		2

/**
 * Acceleration 1g in internal emulator units. It is helpful for using
 * functions @ref bma_emul_set_acc @ref bma_emul_get_acc
 * @ref bma_emul_set_off and @ref bma_emul_get_off
 */
#define BMA_EMUL_1G		BIT(10)

/**
 * Special register values used in @ref bma_emul_set_read_fail_reg and
 * @ref bma_emul_set_write_fail_reg
 */
#define BMA_EMUL_FAIL_ALL_REG	(-1)
#define BMA_EMUL_NO_FAIL_REG	(-2)

/**
 * @brief Get pointer to BMA255 emulator using device tree order number.
 *
 * @param ord Device tree order number obtained from DT_DEP_ORD macro
 *
 * @return Pointer to BMA255 emulator
 */
struct i2c_emul *bma_emul_get(int ord);

/**
 * @brief Custom function type that is used as user-defined callback in read
 *        I2C messages handling.
 *
 * @param emul Pointer to BMA255 emulator
 * @param reg Address which is now accessed by read command
 * @param data Pointer to custom user data
 *
 * @return 0 on success. Value of @p reg should be set by @ref bma_emul_set_reg
 * @return 1 continue with normal BMA255 emulator handler
 * @return negative on error
 */
typedef int (*bma_emul_read_func)(struct i2c_emul *emul, int reg, void *data);

/**
 * @brief Custom function type that is used as user-defined callback in write
 *        I2C messages handling.
 *
 * @param emul Pointer to BMA255 emulator
 * @param reg Address which is now accessed by write command
 * @param val Value which is being written to @p reg
 * @param data Pointer to custom user data
 *
 * @return 0 on success
 * @return 1 continue with normal BMA255 emulator handler
 * @return negative on error
 */
typedef int (*bma_emul_write_func)(struct i2c_emul *emul, int reg, uint8_t val,
				   void *data);

/**
 * @brief Lock access to BMA255 properties. After acquiring lock, user
 *        may change emulator behaviour in multi-thread setup.
 *
 * @param emul Pointer to BMA255 emulator
 * @param timeout Timeout in getting lock
 *
 * @return k_mutex_lock return code
 */
int bma_emul_lock_data(struct i2c_emul *emul, k_timeout_t timeout);

/**
 * @brief Unlock access to BMA255 properties.
 *
 * @param emul Pointer to BMA255 emulator
 *
 * @return k_mutex_unlock return code
 */
int bma_emul_unlock_data(struct i2c_emul *emul);

/**
 * @brief Set write handler for I2C messages. This function is called before
 *        generic handler.
 *
 * @param emul Pointer to BMA255 emulator
 * @param func Pointer to custom function
 * @param data User data passed on call of custom function
 */
void bma_emul_set_write_func(struct i2c_emul *emul, bma_emul_write_func func,
			     void *data);

/**
 * @brief Set read handler for I2C messages. This function is called before
 *        generic handler.
 *
 * @param emul Pointer to BMA255 emulator
 * @param func Pointer to custom function
 * @param data User data passed on call of custom function
 */
void bma_emul_set_read_func(struct i2c_emul *emul, bma_emul_read_func func,
			    void *data);

/**
 * @brief Set value of given register of BMA255
 *
 * @param emul Pointer to BMA255 emulator
 * @param reg Register address which value will be changed
 * @param val New value of the register
 */
void bma_emul_set_reg(struct i2c_emul *emul, int reg, uint8_t val);

/**
 * @brief Get value of given register of BMA255
 *
 * @param emul Pointer to BMA255 emulator
 * @param reg Register address
 *
 * @return Value of the register
 */
uint8_t bma_emul_get_reg(struct i2c_emul *emul, int reg);

/**
 * @brief Setup fail on read of given register of BMA255
 *
 * @param emul Pointer to BMA255 emulator
 * @param reg Register address or one of special values (BMA_EMUL_FAIL_ALL_REG,
 *            BMA_EMUL_NO_FAIL_REG)
 */
void bma_emul_set_read_fail_reg(struct i2c_emul *emul, int reg);

/**
 * @brief Setup fail on write of given register of BMA255
 *
 * @param emul Pointer to BMA255 emulator
 * @param reg Register address or one of special values (BMA_EMUL_FAIL_ALL_REG,
 *            BMA_EMUL_NO_FAIL_REG)
 */
void bma_emul_set_write_fail_reg(struct i2c_emul *emul, int reg);

/**
 * @brief Get internal value of offset for given axis
 *
 * @param emul Pointer to BMA255 emulator
 * @param axis Axis to access: 0 - X, 1 - Y, 2 - Z
 *
 * @return Offset of given axis. LSB is 0.97mg
 */
int16_t bma_emul_get_off(struct i2c_emul *emul, int axis);

/**
 * @brief Set internal value of offset for given axis
 *
 * @param emul Pointer to BMA255 emulator
 * @param axis Axis to access: 0 - X, 1 - Y, 2 - Z
 * @param val New value of offset. LSB is 0.97mg
 */
void bma_emul_set_off(struct i2c_emul *emul, int axis, int16_t val);

/**
 * @brief Get internal value of accelerometer for given axis
 *
 * @param emul Pointer to BMA255 emulator
 * @param axis Axis to access: 0 - X, 1 - Y, 2 - Z
 *
 * @return Acceleration of given axis. LSB is 0.97mg
 */
int16_t bma_emul_get_acc(struct i2c_emul *emul, int axis);

/**
 * @brief Set internal value of accelerometr for given axis
 *
 * @param emul Pointer to BMA255 emulator
 * @param axis Axis to access: 0 - X, 1 - Y, 2 - Z
 * @param val New value of accelerometer axis. LSB is 0.97mg
 */
void bma_emul_set_acc(struct i2c_emul *emul, int axis, int16_t val);

/**
 * @brief Set if error should be generated when fast compensation is triggered
 *        when not ready flag is set
 *
 * @param emul Pointer to BMA255 emulator
 * @param set Check for this error
 */
void bma_emul_set_err_on_cal_nrdy(struct i2c_emul *emul, bool set);

/**
 * @brief Set if error should be generated when fast compensation is triggered
 *        when range is not 2G
 *
 * @param emul Pointer to BMA255 emulator
 * @param set Check for this error
 */
void bma_emul_set_err_on_cal_bad_range(struct i2c_emul *emul, bool set);

/**
 * @brief Set if error should be generated when read only register is being
 *        written
 *
 * @param emul Pointer to BMA255 emulator
 * @param set Check for this error
 */
void bma_emul_set_err_on_ro_write(struct i2c_emul *emul, bool set);

/**
 * @brief Set if error should be generated when reserved bits of register are
 *        not set to 0 on write I2C message
 *
 * @param emul Pointer to BMA255 emulator
 * @param set Check for this error
 */
void bma_emul_set_err_on_rsvd_write(struct i2c_emul *emul, bool set);

/**
 * @brief Set if error should be generated when MSB register is accessed before
 *        LSB register
 *
 * @param emul Pointer to BMA255 emulator
 * @param set Check for this error
 */
void bma_emul_set_err_on_msb_first(struct i2c_emul *emul, bool set);

/**
 * @}
 */

#endif /* __EMUL_BMA255_H */
