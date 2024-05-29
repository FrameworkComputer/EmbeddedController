/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

/**
 * @file
 *
 * @brief Common code used by devices emulated on I2C bus
 */

#ifndef __EMUL_COMMON_I2C_H
#define __EMUL_COMMON_I2C_H

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

/**
 * @brief Common I2C API useb by emulators
 * @defgroup i2c_common_emul common I2C emulator's code
 * @{
 *
 * I2C common emulator functionality is dispatching I2C messages. It supports
 * setting custom user handler and selecting register on which access emulator
 * should fail. To use common I2C handling, emulator should call or setup
 * @ref i2c_common_emul_transfer as transfer callback of i2c_emul_api and
 * register emulator with @ref i2c_common_emul_data structure as data. In data
 * structure, emualtor should set callback called before read/write I2C message
 * (start_read, start_write), for each byte of I2C message (read_byte,
 * write_byte) and after I2C message (finish_read, finish_byte). If specific
 * function is not needed by emulator, than it can be set to NULL.
 *
 * @ref i2c_common_emul_lock_data and @ref i2c_common_emul_unlock_data functions
 * may be used to guard emulator data when accessed from multiple threads.
 *
 * User of emulator with common I2C code can use following API to define custom
 * behaviour of emulator:
 *
 * - call @ref i2c_common_emul_set_read_func and
 *   @ref i2c_common_emul_set_write_func to setup custom handlers for I2C
 *   messages
 * - call @ref i2c_common_emul_set_read_fail_reg and
 *   @ref i2c_common_emul_set_write_fail_reg to configure emulator to fail on
 *   given register read or write
 */

/**
 * Special register values used in @ref i2c_common_emul_set_read_fail_reg and
 * @ref i2c_common_emul_set_write_fail_reg
 */
#define I2C_COMMON_EMUL_FAIL_ALL_REG (-1)
#define I2C_COMMON_EMUL_NO_FAIL_REG (-2)

/**
 * Describe if there is no ongoing I2C message or if there is message handled
 * at the moment (last message doesn't ended with stop or write is not followed
 * by read).
 */
enum i2c_common_emul_msg_state {
	I2C_COMMON_EMUL_NONE_MSG,
	I2C_COMMON_EMUL_IN_WRITE,
	I2C_COMMON_EMUL_IN_READ
};

/**
 * @brief Function type that is used by I2C device emulator for first byte of
 *        I2C write message.
 *
 * @param target Pointer to emulator
 * @param reg Address which is now accessed by write command (first byte of I2C
 *            write message)
 *
 * @return 0 on success
 * @return -EIO on error
 */
typedef int (*i2c_common_emul_start_write_func)(const struct emul *target,
						int reg);

/**
 * @brief Function type that is used by I2C device emulator at the end of
 *        I2C write message.
 *
 * @param target Pointer to emulator
 * @param reg Address which is now accessed by write command (first byte of I2C
 *            write message)
 * @param bytes Number of bytes received from the I2C write message
 *
 * @return 0 on success
 * @return -EIO on error
 */
typedef int (*i2c_common_emul_finish_write_func)(const struct emul *target,
						 int reg, int bytes);

/**
 * @brief Function type that is used by I2C device emulator on each byte of
 *        I2C write message (except first byte).
 *
 * @param target Pointer to emulator
 * @param reg Address which is now accessed by write command (first byte of I2C
 *            write message)
 * @param val Value of current byte
 * @param bytes Number of bytes already received from the I2C write message
 *              (excluding current byte)
 *
 * @return 0 on success
 * @return -EIO on error
 */
typedef int (*i2c_common_emul_write_byte_func)(const struct emul *target,
					       int reg, uint8_t val, int bytes);

/**
 * @brief Function type that is used by I2C device emulator before first byte of
 *        I2C read message.
 *
 * @param target Pointer to emulator
 * @param reg Address which is now accessed by read command (first byte of last
 *            I2C write message)
 *
 * @return 0 on success
 * @return -EIO on error
 */
typedef int (*i2c_common_emul_start_read_func)(const struct emul *target,
					       int reg);

/**
 * @brief Function type that is used by I2C device emulator at the end of
 *        I2C read message.
 *
 * @param target Pointer to emulator
 * @param reg Address which is now accessed by read command (first byte of last
 *            I2C write message)
 * @param bytes Number of bytes responeded to the I2C read message
 *
 * @return 0 on success
 * @return -EIO on error
 */
typedef int (*i2c_common_emul_finish_read_func)(const struct emul *target,
						int reg, int bytes);

/**
 * @brief Function type that is used by I2C device emulator on each byte of
 *        I2C read message.
 *
 * @param target Pointer to emulator
 * @param reg Address which is now accessed by read command (first byte of last
 *            I2C write message)
 * @param val Pointer to buffer where current response byte should be stored
 * @param bytes Number of bytes already responded to the I2C read message
 *              (excluding current byte)
 *
 * @return 0 on success
 * @return -EIO on error
 */
typedef int (*i2c_common_emul_read_byte_func)(const struct emul *target,
					      int reg, uint8_t *val, int bytes);

/**
 * @brief Function type that is used by I2C device emulator to select register
 *        address that should be compared with fail register set by user using
 *        @ref i2c_common_emul_set_read_fail_reg and
 *        @ref i2c_common_emul_set_write_fail_reg
 *
 * @param target Pointer to emulator
 * @param reg Address which is now accessed by read/write command (first byte
 *            of last I2C write message)
 * @param bytes Number of bytes already processed in the I2C message handler
 *              (excluding current byte)
 * @param read If current I2C message is read
 *
 * @return Register address that should be compared with user-defined fail
 *         register
 */
typedef int (*i2c_common_emul_access_reg_func)(const struct emul *target,
					       int reg, int bytes, bool read);

/**
 * @brief Custom function type that is used as user-defined callback in read
 *        I2C messages handling.
 *
 * @param target Pointer to emulator
 * @param reg Address which is now accessed by read command (first byte of last
 *            I2C write message)
 * @param val Pointer to buffer where current response byte should be stored
 * @param bytes Number of bytes already responded to the I2C read message
 *              (excluding current byte)
 * @param data Pointer to custom user data
 *
 * @return 0 on success
 * @return 1 continue with normal emulator handler
 * @return negative on error
 */
typedef int (*i2c_common_emul_read_func)(const struct emul *target, int reg,
					 uint8_t *val, int bytes, void *data);

/**
 * @brief Custom function type that is used as user-defined callback in write
 *        I2C messages handling.
 *
 * @param target Pointer to emulator
 * @param reg Address which is now accessed by write command (first byte of I2C
 *            write message)
 * @param val Value of current byte
 * @param bytes Number of bytes already received from the I2C write message
 *              (excluding current byte)
 * @param data Pointer to custom user data
 *
 * @return 0 on success
 * @return 1 continue with normal emulator handler
 * @return negative on error
 */
typedef int (*i2c_common_emul_write_func)(const struct emul *target, int reg,
					  uint8_t val, int bytes, void *data);

/** Static configuration, common for all i2c emulators */
struct i2c_common_emul_cfg {
	/** Label of the I2C device being emulated */
	const char *dev_label;
	/** Pointer to run-time data */
	struct i2c_common_emul_data *data;
	/** Address of emulator on i2c bus */
	uint16_t addr;
};

/** Run-time data used by the emulator, common for all i2c emulators */
struct i2c_common_emul_data {
	/** I2C emulator detail */
	struct i2c_emul emul;
	/** Emulator device */
	const struct device *i2c;
	/** Configuration information */
	const struct i2c_common_emul_cfg *cfg;

	/** Current state of I2C bus (if emulator is handling message) */
	enum i2c_common_emul_msg_state msg_state;
	/** Number of already handled bytes in ongoing message */
	int msg_byte;
	/** Register selected in last write command */
	uint8_t cur_reg;

	/** Custom write function called on I2C write opperation */
	i2c_common_emul_write_func write_func;
	/** Data passed to custom write function */
	void *write_func_data;
	/** Custom read function called on I2C read opperation */
	i2c_common_emul_read_func read_func;
	/** Data passed to custom read function */
	void *read_func_data;

	/** Control if read should fail on given register */
	int read_fail_reg;
	/** Control if write should fail on given register */
	int write_fail_reg;

	/** Emulator function, called for first byte of write message */
	i2c_common_emul_start_write_func start_write;
	/** Emulator function, called for each byte of write message */
	i2c_common_emul_write_byte_func write_byte;
	/** Emulator function, called at the end of write message */
	i2c_common_emul_finish_write_func finish_write;

	/** Emulator function, called before first byte of read message */
	i2c_common_emul_start_read_func start_read;
	/** Emulator function, called for each byte of read message */
	i2c_common_emul_read_byte_func read_byte;
	/** Emulator function, called at the end of read message */
	i2c_common_emul_finish_read_func finish_read;

	/**
	 * Emulator function, called to get register that should be checked
	 * if was selected by user in set_read/write_fail_reg.
	 */
	i2c_common_emul_access_reg_func access_reg;

	/** Mutex used to control access to emulator data */
	struct k_mutex data_mtx;
};

/** A common API that simply links to the i2c_common_emul_transfer function */
extern struct i2c_emul_api i2c_common_emul_api;

/**
 * @brief Lock access to emulator properties. After acquiring lock, user
 *        may change emulator behaviour in multi-thread setup.
 *
 * @param common_data Pointer to emulator common data
 * @param timeout Timeout in getting lock
 *
 * @return k_mutex_lock return code
 */
int i2c_common_emul_lock_data(struct i2c_common_emul_data *common_data,
			      k_timeout_t timeout);

/**
 * @brief Unlock access to emulator properties.
 *
 * @param common_data Pointer to emulator common data
 *
 * @return k_mutex_unlock return code
 */
int i2c_common_emul_unlock_data(struct i2c_common_emul_data *common_data);

/**
 * @brief Set write handler for I2C messages. This function is called before
 *        generic handler.
 *
 * @param common_data Pointer to emulator common data
 * @param func Pointer to custom function
 * @param data User data passed on call of custom function
 */
void i2c_common_emul_set_write_func(struct i2c_common_emul_data *common_data,
				    i2c_common_emul_write_func func,
				    void *data);

/**
 * @brief Set read handler for I2C messages. This function is called before
 *        generic handler.
 *
 * @param common_data Pointer to emulator common data
 * @param func Pointer to custom function
 * @param data User data passed on call of custom function
 */
void i2c_common_emul_set_read_func(struct i2c_common_emul_data *common_data,
				   i2c_common_emul_read_func func, void *data);

/**
 * @brief Setup fail on read of given register of emulator
 *
 * @param common_data Pointer to emulator common data
 * @param reg Register address or one of special values
 *            (I2C_COMMON_EMUL_FAIL_ALL_REG, I2C_COMMON_EMUL_NO_FAIL_REG)
 */
void i2c_common_emul_set_read_fail_reg(struct i2c_common_emul_data *common_data,
				       int reg);

/**
 * @brief Setup fail on write of given register of emulator
 *
 * @param common_data Pointer to emulator common data
 * @param reg Register address or one of special values
 *            (I2C_COMMON_EMUL_FAIL_ALL_REG, I2C_COMMON_EMUL_NO_FAIL_REG)
 */
void i2c_common_emul_set_write_fail_reg(
	struct i2c_common_emul_data *common_data, int reg);

/**
 * @biref Emulate an I2C transfer to an emulator
 *
 * This is common function used by I2C device emulators. It handles dispatching
 * I2C message, calling user custom functions, failing on reading/writing
 * registers selected by user and calling device specific functions.
 *
 * @param target The target peripheral emulated
 * @param msgs List of messages to process
 * @param num_msgs Number of messages to process
 * @param addr Address of the I2C target device
 *
 * @retval 0 If successful
 * @retval -EIO General input / output error
 */
int i2c_common_emul_transfer(const struct emul *target, struct i2c_msg *msgs,
			     int num_msgs, int addr);

int i2c_common_emul_transfer_workhorse(const struct emul *target,
				       struct i2c_common_emul_data *data,
				       const struct i2c_common_emul_cfg *cfg,
				       struct i2c_msg *msgs, int num_msgs,
				       int addr);

/**
 * @brief Initialize common emulator data structure
 *
 * @param data Pointer to emulator data
 */
void i2c_common_emul_init(struct i2c_common_emul_data *data);

/**
 * @}
 */

#endif /* __EMUL_COMMON_I2C_H */
