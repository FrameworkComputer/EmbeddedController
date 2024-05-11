/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PECI module for Chrome EC */

#ifndef __CROS_EC_PECI_H
#define __CROS_EC_PECI_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PECI_TARGET_ADDRESS 0x30
#define PECI_WRITE_DATA_FIFO_SIZE 15
#define PECI_READ_DATA_FIFO_SIZE 16

#define PECI_GET_TEMP_READ_LENGTH 2
#define PECI_GET_TEMP_WRITE_LENGTH 0
#define PECI_GET_TEMP_TIMEOUT_US 200

/* PECI Command Code */
enum peci_command_code {
	PECI_CMD_PING = 0x00,
	PECI_CMD_GET_DIB = 0xF7,
	PECI_CMD_GET_TEMP = 0x01,
	PECI_CMD_RD_PKG_CFG = 0xA1,
	PECI_CMD_WR_PKG_CFG = 0xA5,
	PECI_CMD_RD_IAMSR = 0xB1,
	PECI_CMD_WR_IAMSR = 0xB5,
	PECI_CMD_RD_PCI_CFG = 0x61,
	PECI_CMD_WR_PCI_CFG = 0x65,
	PECI_CMD_RD_PCI_CFG_LOCAL = 0xE1,
	PECI_CMD_WR_PCI_CFG_LOCAL = 0xE5,
};

struct peci_data {
	enum peci_command_code cmd_code; /* command code */
	uint8_t addr; /* client address */
	uint8_t w_len; /* write length */
	uint8_t r_len; /* read length */
	uint8_t *w_buf; /* buffer pointer of write data */
	uint8_t *r_buf; /* buffer pointer of read data */
	int timeout_us; /* transaction timeout unit:us */
};

/**
 * Get the last polled value of the PECI temp sensor.
 *
 * @param idx		Sensor index to read.
 * @param temp_ptr	Destination for temperature in K.
 *
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int peci_temp_sensor_get_val(int idx, int *temp_ptr);

/**
 * Start a PECI transaction
 *
 * @param  peci transaction data
 *
 * @return zero if successful, non-zero if error
 */
int peci_transaction(struct peci_data *peci);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_PECI_H */
