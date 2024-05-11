/* Copyright 2011 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* EEPROM module for Chrome EC */

#ifndef __CROS_EC_EEPROM_H
#define __CROS_EC_EEPROM_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the module.
 */
int eeprom_init(void);

/**
 * Returns the number of EEPROM blocks on the system.
 */
int eeprom_get_block_count(void);

/**
 * Return the EEPROM block size in bytes.
 */
int eeprom_get_block_size(void);

/**
 * Read data from EEPROM.
 *
 * @param block		Block number
 * @param offset	Byte offset in block; must be multiple of 4.
 * @param size		Number of bytes to read; must be multiple of 4.
 * @param data		Destination for data
 */
int eeprom_read(int block, int offset, int size, char *data);

/**
 * Write data to EEPROM.
 *
 * @param block		Block number
 * @param offset	Byte offset in block; must be multiple of 4.
 * @param size		Number of bytes to write; must be multiple of 4.
 * @param data		Data to write
 */
int eeprom_write(int block, int offset, int size, const char *data);

/**
 * Hide an EEPROM block until the next reset.
 *
 * @param block		Block number
 */
int eeprom_hide(int block);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_EEPROM_H */
