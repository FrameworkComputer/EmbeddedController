/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_EC_FLASH_H
#define __CROS_EC_EC_FLASH_H

/**
 * Read EC flash memory
 *
 * @param buf		Destination buffer
 * @param offset	Offset in EC flash to read
 * @param size		Number of bytes to read
 *
 * @return 0 if success, negative if error.
 */
int ec_flash_read(uint8_t *buf, int offset, int size);

/**
 * Verify EC flash memory
 *
 * @param buf		Source buffer to verify against EC flash
 * @param offset	Offset in EC flash to check
 * @param size		Number of bytes to check
 *
 * @return 0 if success, negative if error.
 */
int ec_flash_verify(const uint8_t *buf, int offset, int size);

/**
 * Write EC flash memory
 *
 * @param buf		Source buffer
 * @param offset	Offset in EC flash to write
 * @param size		Number of bytes to write
 *
 * @return 0 if success, negative if error.
 */
int ec_flash_write(const uint8_t *buf, int offset, int size);

/**
 * Erase EC flash memory
 *
 * @param offset	Offset in EC flash to erase
 * @param size		Number of bytes to erase
 *
 * @return 0 if success, negative if error.
 */
int ec_flash_erase(int offset, int size);

#endif
