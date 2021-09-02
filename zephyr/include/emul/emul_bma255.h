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
 * - call @ref bma_emul_set_reg and @ref bma_emul_get_reg to set and get value
 *   of BMA255 registers
 * - call @ref bma_emul_set_off and @ref bma_emul_set_off to set and get
 *   internal offset value
 * - call @ref bma_emul_set_acc and @ref bma_emul_set_acc to set and get
 *   accelerometer value
 * - call bma_emul_set_err_* to change emulator behaviour on inadvisable driver
 *   behaviour
 * - call functions from emul_common_i2c.h to setup custom handlers for I2C
 *   messages
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
 * @brief Get pointer to BMA255 emulator using device tree order number.
 *
 * @param ord Device tree order number obtained from DT_DEP_ORD macro
 *
 * @return Pointer to BMA255 emulator
 */
struct i2c_emul *bma_emul_get(int ord);

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
 * @brief Function calculate register that should be accessed when I2C message
 *        started from @p reg register and now byte number @p bytes is handled.
 *        This function is used in I2C common emulator code and can be used in
 *        custom user functions.
 *
 * @param emul Pointer to BMA255 emulator
 * @param reg Starting register
 * @param bytes Number of bytes already processed in the I2C message handler
 * @param read If current I2C message is read
 *
 * @retval Register address that should be accessed
 */
int bma_emul_access_reg(struct i2c_emul *emul, int reg, int bytes, bool read);

/**
 * @}
 */

#endif /* __EMUL_BMA255_H */
