/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* EEPROM module for Chrome EC */

#ifndef __CROS_EC_EEPROM_H
#define __CROS_EC_EEPROM_H

#include "common.h"

/* Initializes the module. */
int eeprom_init(void);

/* Returns the number of EEPROM blocks on the system. */
int eeprom_get_block_count(void);

/* Returns the EEPROM block size in bytes. */
int eeprom_get_block_size(void);

/* Reads <size> bytes of data from <offset> in <block> of EEPROM.  Offset
 * and size must be a multiple of 32 bits. */
int eeprom_read(int block, int offset, int size, char *data);

/* Writes <size> bytes of data to <offset> in <block> of EEPROM.  Offset
 * and size must be a multiple of 32 bits. */
int eeprom_write(int block, int offset, int size, const char *data);

/* Hides an EEPROM block until the next reset. */
int eeprom_hide(int block);

/* TODO: write protect */

#endif  /* __CROS_EC_EEPROM_H */
