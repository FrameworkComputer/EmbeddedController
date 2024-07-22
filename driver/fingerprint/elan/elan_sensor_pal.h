/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* ELAN Platform Abstraction Layer callbacks */

#ifndef ELAN_SENSOR_PAL_H_
#define ELAN_SENSOR_PAL_H_

#include "common.h"

/* ELAN error codes */
enum elan_error_code {
	ELAN_ERROR_NONE = 0,
	ELAN_ERROR_SPI = 1,
	ELAN_ERROR_SCAN = 2,
	ELAN_ERROR_CAL = 3,
	ELAN_ERROR_DEFECT_NUM = 4,
	ELAN_ERROR_DEFECT_X = 5,
	ELAN_ERROR_DEFECT_Y = 6
};

/* ELAN error info */
typedef struct {
	uint32_t num_defective_pixels;
	uint16_t sensor_error_code;
} fp_sensor_info_t;

/**
 * @brief Write fp command to the sensor
 *
 * Unused by staticlib.
 *
 * @param[in] fp_cmd     One byte fp command to write
 *
 * @return 0 on success.
 *         negative value on error.
 */
int elan_write_cmd(uint8_t fp_cmd);

/**
 * @brief Read fp register data from the sensor
 *
 * @param[in]   fp_cmd   One byte fp command to read
 * @param[out]  regdata  One byte data where register's data will be stored
 *
 * @return 0 on success.
 *         negative value on error.
 */
__staticlib_hook int elan_read_cmd(uint8_t fp_cmd, uint8_t *regdata);

/**
 * @brief Transfers and receives SPI data.
 *
 * Unused by staticlib.
 *
 * @param[in]   tx              The buffer to transfer
 * @param[in]   tx_len          The length to transfer
 * @param[out]  rx              The buffer where read data will be stored
 * @param[in]   rx_len          The length to receive
 * @return 0 on success.
 *         negative value on error.
 */
int elan_spi_transaction(uint8_t *tx, int tx_len, uint8_t *rx, int rx_len);

/**
 * @brief Write fp register data to sensor
 *
 * @param[in]    regaddr  One byte register address to write
 * @param[in]    regdata  Data to write to register
 *
 * @return 0 on success.
 *         negative value on error.
 */
__staticlib_hook int elan_write_register(uint8_t regaddr, uint8_t regdata);

/**
 * @brief Read fp register data from sensor
 *
 * @param[in]    regaddr  One byte register address to read
 * @param[out]   regdata  Single byte value read from register
 *
 * @return 0 on success.
 *         negative value on error.
 */
int elan_read_register(uint8_t regaddr, uint8_t *regdata);

/**
 * @brief Select sensor RAM page of register
 *
 * @param[in]   page    The number of RAM page control registers
 *
 * @return 0 on success.
 *         negative value on error.
 */
__staticlib_hook int elan_write_page(uint8_t page);

/**
 * @brief Write register table to fp sensor
 *
 * Using a table to write data to sensor register.
 * This table contains multiple pairs of address and data to
 * be written.
 *
 * @param[in]    reg_table       The register address to write
 * @param[in]    length          The data to write to register
 *
 * @return 0 on success.
 *         negative value on error.
 */
__staticlib_hook int elan_write_reg_vector(const uint8_t *reg_table,
					   int length);

/**
 * Get 14bits raw image data from ELAN fingerprint sensor
 *
 * @param[out] short_raw    The memory buffer to receive fingerprint image
 *                          raw data, buffer length is:
 *                          (IMAGE_WIDTH*IMAGE_HEIGHT)*sizeof(uint16_t)
 *
 * @return 0 on success.
 *         negative value on error.
 */
__staticlib_hook int elan_raw_capture(uint16_t *short_raw);

/**
 * Execute calibrate ELAN fingerprint sensor flow.
 *
 * @return 0 on success.
 *         negative value on error.
 */
__staticlib_hook int elan_execute_calibration(void);

/**
 * Execute reset ELAN fingerprint sensor flow.
 */
__staticlib void elan_execute_reset(void);

/**
 * Runs a test for defective pixels.
 *
 * @param[out] fp_sensor_info  Structure containing output data.
 *
 * @return 0 on success.
 *         negative value on error.
 */
__staticlib int elan_fp_sensor_maintenance(fp_sensor_info_t *fp_sensor_info);

/**
 * @brief Set sensor reset state.
 *
 * Set sensor reset state.
 *
 * @param[in] state Reset state.
 *                  true  => reset sensor, i.e. low GPIO state
 *                  false => normal operation, i.e. high GPIO state
 */
__staticlib_hook void elan_sensor_set_rst(bool state);

/**
 * Enable or disable settings of high voltage chip for ELAN fingerprint sensor
 *
 * This can only be used on the EFSA80SG.
 *
 * @param[in] state The state of high voltage chip
 *                  true  => enable settings of high voltage chip
 *                  false => disable settings of high voltage chip
 *
 * @return 0 on success.
 *         negative value on error.
 */
int elan_set_hv_chip(bool state);

/**
 * @brief Sleep
 *
 * @param[in] us    Number of microseconds to sleep.
 * @return 0 on success, negative on error
 */
__staticlib_hook int elan_usleep(unsigned int us);
#endif
