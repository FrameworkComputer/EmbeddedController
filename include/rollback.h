/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ROLLBACK_H
#define __CROS_EC_ROLLBACK_H

#define CROS_EC_ROLLBACK_COOKIE 0x0b112233

#ifndef __ASSEMBLER__

/**
 * Get minimum version set by rollback protection blocks.
 *
 * @return Minimum rollback version, 0 if neither block is initialized,
 * negative value on error.
 */
int rollback_get_minimum_version(void);

/**
 * Update rollback protection block to the version passed as parameter.
 *
 * @param next_min_version	Minimum version to write in rollback block.
 *
 * @return EC_SUCCESS on success, EC_ERROR_* on error.
 */
int rollback_update_version(int32_t next_min_version);

/**
 * Add entropy to the rollback block.
 *
 * @param data	Data to be added to rollback block secret (after hashing)
 * @param len	data length
 *
 * @return EC_SUCCESS on success, EC_ERROR_* on error.
 */
int rollback_add_entropy(uint8_t *data, unsigned int len);

/**
 * Lock rollback protection block, reboot if necessary.
 *
 * @return EC_SUCCESS if rollback was already protected.
 */
int rollback_lock(void);

#endif

#endif  /* __CROS_EC_ROLLBACK_H */
