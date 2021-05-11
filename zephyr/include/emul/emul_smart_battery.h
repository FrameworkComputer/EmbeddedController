/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for Smart Battery emulator
 */

#ifndef __EMUL_SMART_BATTERY_H
#define __EMUL_SMART_BATTERY_H

#include <emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

/**
 * @brief Smart Battery emulator backend API
 * @defgroup sbat_emul Smart Battery emulator
 * @{
 *
 * Smart Battery emulator handle static state of device. E.g. setting charging
 * current will not charge battery over time. Sending periodic status messages
 * and alarms to SMBus Host or charging voltage/current to Smart Battery Charger
 * is not supported. Behaviour of Smart Battery emulator is application-defined.
 * As-such, each application may
 *
 * - define a Device Tree overlay file to set the most of battery properties
 * - call @ref sbat_emul_set_custom_write_func and
 *   @ref sbat_emul_set_custom_read_func to setup custom handlers for SMBus
 *   messages.
 * - get battery properties calling @ref sbat_emul_get_bat_data Battery
 *   properties can be changed through obtained pointer. In multithread
 *   environment access to battery can be guarded by calling
 *   @ref sbat_emul_lock_bat_data and @ref sbat_emul_unlock_bat_data
 */

/* Value used to indicate that no command is selected */
#define SBAT_EMUL_NO_CMD	-1
/* Maximum size of data that can be returned in SMBus block transaction */
#define MAX_BLOCK_SIZE		32
/* Maximum length of command to send is maximum size of data + len byte + PEC */
#define MSG_BUF_LEN		(MAX_BLOCK_SIZE + 2)

/** @brief Emulated smart battery properties */
struct sbat_emul_bat_data {
	/** Battery mode - bit field configuring some battery behaviours */
	uint16_t mode;
	/** Word returned on manufacturer access command */
	uint16_t mf_access;
	/** Capacity alarm value */
	uint16_t cap_alarm;
	/** Remaing time alarm value */
	uint16_t time_alarm;
	/** Rate of charge used in some commands */
	int16_t at_rate;
	/**
	 * Flag indicating if AT_RATE_TIME_TO_FULL command supports mW
	 * capacity mode
	 */
	int at_rate_full_mw_support;
	/** Error code returned by last command */
	uint16_t error_code;
	/** Design battery voltage in mV */
	uint16_t design_mv;
	/** Battery temperature at the moment in Kelvins */
	uint16_t temp;
	/** Battery voltage at the moment in mV */
	uint16_t volt;
	/** Current charging (> 0) or discharging (< 0) battery in mA */
	int16_t cur;
	/** Average current from 1 minute */
	int16_t avg_cur;
	/** Maximum error of returned values in percent */
	uint16_t max_error;
	/** Capacity of the battery at the moment in mAh */
	uint16_t cap;
	/** Full capacity of the battery in mAh */
	uint16_t full_cap;
	/** Design battery capacity in mAh */
	uint16_t design_cap;
	/** Charging current requested by battery */
	uint16_t desired_charg_cur;
	/** Charging voltage requested by battery */
	uint16_t desired_charg_volt;
	/** Number of cycles */
	uint16_t cycle_count;
	/** Specification of battery */
	uint16_t spec_info;
	/** Status of battery */
	uint16_t status;
	/** Date of manufacturing */
	uint16_t mf_date;
	/** Serial number */
	uint16_t sn;
	/** Manufacturer name */
	uint8_t mf_name[MAX_BLOCK_SIZE];
	/** Manufacturer name length */
	int mf_name_len;
	/** Device name */
	uint8_t dev_name[MAX_BLOCK_SIZE];
	/** Device name length */
	int dev_name_len;
	/** Device chemistry */
	uint8_t dev_chem[MAX_BLOCK_SIZE];
	/** Device chemistry length */
	int dev_chem_len;
	/** Manufacturer data */
	uint8_t mf_data[MAX_BLOCK_SIZE];
	/** Manufacturer data length */
	int mf_data_len;
};

/**
 * @brief Get pointer to smart battery emulator using device tree order number.
 *
 * @param ord Device tree order number obtained from DT_DEP_ORD macro
 *
 * @return Pointer to smart battery emulator
 */
struct i2c_emul *sbat_emul_get_ptr(int ord);

/**
 * @brief Custom function type that is used as user defined callbacks in read
 *        and write SMBus messages handling.
 *
 * @param emul Pointer to smart battery emulator
 * @param buf Pointer to data from write command or to be filed by read command
 * @param len Pointer to number of bytes used for write command buffer. It may
 *            exceed MSG_BUF_LEN, indicating that some bytes from write command
 *            are not saved in @p buf. If read command is handled, than
 *            function should set how many bytes are written to @p buf
 * @param cmd Command that was recognized
 * @param data Pointer to custom user data
 *
 * @return 0 on success
 * @return 1 continue with normal smart battery emulator handler
 * @return negative on error
 */
typedef int (*sbat_emul_custom_func)(struct i2c_emul *emul, uint8_t *buf,
				     int *len, int cmd, void *data);

/**
 * @brief Function which allows to get properties of emulated smart battery
 *
 * @param emul Pointer to smart battery emulator
 *
 * @return Pointer to smart battery properties
 */
struct sbat_emul_bat_data *sbat_emul_get_bat_data(struct i2c_emul *emul);

/**
 * @brief Lock access to smart battery properties. After acquiring lock, user
 *        may change emulator behaviour in multi-thread setup.
 *
 * @param emul Pointer to smart battery emulator
 * @param timeout Timeout in getting lock
 *
 * @return k_mutex_lock return code
 */
int sbat_emul_lock_bat_data(struct i2c_emul *emul, k_timeout_t timeout);

/**
 * @brief Unlock access to smart battery properties.
 *
 * @param emul Pointer to smart battery emulator
 *
 * @return k_mutex_unlock return code
 */
int sbat_emul_unlock_bat_dat(struct i2c_emul *emul);

/**
 * @brief Set custom write SMBus message handler. This function is called before
 *        generic handler.
 *
 * @param emul Pointer to smart battery emulator
 * @param func Pointer to custom function
 * @param data User data passed on call of custom function
 */
void sbat_emul_set_custom_write_func(struct i2c_emul *emul,
				     sbat_emul_custom_func func, void *data);

/**
 * @brief Set custom read SMBus message handler. This function is called before
 *        generic handler.
 *
 * @param emul Pointer to smart battery emulator
 * @param func Pointer to custom function
 * @param data User data passed on call of custom function
 */
void sbat_emul_set_custom_read_func(struct i2c_emul *emul,
				    sbat_emul_custom_func func, void *data);

/**
 * @brief Convert date to format used by smart battery
 *
 * @param day Day
 * @param month Month
 * @param year Year
 *
 * @return Converted date
 */
uint16_t sbat_emul_date_to_word(unsigned int day, unsigned int month,
				unsigned int year);

/**
 * @brief Function which gets return value for read commands that returns word.
 *        This function may be used to obtain battery properties that are
 *        calculated e.g. time to empty/full.
 *
 * @param emul Pointer to smart battery emulator
 * @param cmd Read command
 * @param val Pointer to where word should be stored
 *
 * @return 0 on success
 * @return 1 if command is unknown or return type different then word
 * @return negative on error while reading value
 */
int sbat_emul_get_word_val(struct i2c_emul *emul, int cmd, uint16_t *val);

/**
 * @brief Function which gets return value for read commands that returns block
 *        data
 *
 * @param emul Pointer to smart battery emulator
 * @param cmd Read command
 * @param blk Pointer to where data pointer should be stored
 * @param len Pointer to where data length should be stored
 *
 * @return 0 on success
 * @return 1 if command is unknown or return type different then word
 * @return negative on error while reading value
 */
int sbat_emul_get_block_data(struct i2c_emul *emul, int cmd, uint8_t **blk,
			     int *len);

/**
 * @}
 */

#endif /* __EMUL_SMART_BATTERY_H */
