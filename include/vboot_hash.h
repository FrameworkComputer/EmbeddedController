/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot hashing memory module for Chrome EC */

#ifndef __CROS_EC_VBOOT_HASH_H
#define __CROS_EC_VBOOT_HASH_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get hash of RW image.
 *
 * Your task will be blocked until hash computation is done. Hashing can be
 * aborted only due to internal errors (e.g. read error) but not external
 * causes.
 *
 * This is expected to be called before tasks are initialized. If it's called
 * after tasks are started, it may starve lower priority tasks.
 *
 * See chromium:1047870 for some optimization.
 *
 * @param dst	(OUT) Address where computed hash is stored.
 * @return	enum ec_error_list.
 */
int vboot_get_rw_hash(const uint8_t **dst);

/**
 * @brief Computes the hash of the RO image. This blocks until the hash is
 *        ready.
 *
 * @param dst	(OUT) Address where computed hash is stored.
 * @return	enum ec_error_list.
 */
int vboot_get_ro_hash(const uint8_t **dst);

/**
 * Invalidate the hash if the hashed data overlaps the specified region.
 *
 * @param offset	Region start offset in flash
 * @param size		Size of region in bytes
 *
 * @return non-zero if the region overlapped the hashed region.
 */
int vboot_hash_invalidate(int offset, int size);

/**
 * Get vboot progress status.
 *
 * @return 1 if vboot hashing is in progress, 0 otherwise.
 */
int vboot_hash_in_progress(void);

/**
 * Abort hash currently in progress, and invalidate any completed hash.
 */
void vboot_hash_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_VBOOT_HASH_H */
