/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Misc utilities for fingerprint management */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_UTILS_H
#define __CROS_EC_FPSENSOR_FPSENSOR_UTILS_H

#include "common.h"

#include <cstdint>
#include <string>

/**
 * Test that size+offset does not exceed buffer_size
 *
 * Returns:
 *   EC_ERROR_OVERFLOW: if size+offset does not fit in uint32_t
 *   EC_ERROR_INVAL: if size+offset > buffer_size
 *   EC_SUCCESS: otherwise
 */
enum ec_error_list validate_fp_buffer_offset(uint32_t buffer_size,
					     uint32_t offset, uint32_t size);

bool fp_match_success(int match_result);

/**
 * @param mode sensor mode
 * @return true if the mode is a test capture that does not require finger
 * touch.
 */
bool is_test_capture(uint32_t mode);

/**
 * @param mode sensor mode
 * @return true if the mode is one that yields a frame in which all bytes should
 * be returned over EC_CMD_FRAME.
 * Other captures modes (simple, pattern0, pattern1, and reset_test) are
 * only interested in the height*width*bpp image bytes that are offset inside
 * the frame.
 * These modes correspond to using the ectool fpframe "raw" modifier.
 */
bool is_raw_capture(uint32_t mode);

/**
 * Format an unsigned int FOURCC value as a printable string.
 *
 * If the character is unprintable, we will print '.', instead.
 * This is similar to hexdump's behavior.
 */
const std::string fourcc_to_string(uint32_t value);

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_UTILS_H */
