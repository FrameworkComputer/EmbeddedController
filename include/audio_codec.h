/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_AUDIO_CODEC_H
#define __CROS_EC_AUDIO_CODEC_H

#include "stdint.h"

/*
 * Common abstract layer
 */

/*
 * Checks capability of audio codec.
 *
 * @cap is an integer from enum ec_codec_cap.  Note that it represents a
 * bit field in a 32-bit integer.  The valid range is [0, 31].
 *
 * Returns:
 *   1 if audio codec capabilities include cap passed as parameter.
 *   0 if not capable.
 */
int audio_codec_capable(uint8_t cap);

/*
 * Registers shared memory (SHM).
 *
 * @shm_id is a SHM identifier from enum ec_codec_shm_id.
 * @cap is an integer from enum ec_codec_cap.
 * @addr is the address pointer to the SHM.
 * @len is the maximum length of the SHM.
 * @type is an integer from enum ec_codec_shm_type.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal errors.
 *   EC_ERROR_INVAL if invalid shm_id.
 *   EC_ERROR_INVAL if invalid cap.
 *   EC_ERROR_BUSY if the shm_id has been registered.
 */
int audio_codec_register_shm(uint8_t shm_id, uint8_t cap,
		uintptr_t *addr, uint32_t len, uint8_t type);

/*
 * Translates the physical address from AP to EC's memory space.  Required if
 * wants to use AP SHM.
 *
 * @ap_addr is physical address from AP.
 * @ec_addr is the translation destination.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal errors.
 *   EC_ERROR_UNIMPLEMENTED if no concrete implementation.
 */
int audio_codec_memmap_ap_to_ec(uintptr_t ap_addr, uintptr_t *ec_addr);

#endif
