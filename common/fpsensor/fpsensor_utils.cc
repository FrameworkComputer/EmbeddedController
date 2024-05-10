/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_utils.h"

extern "C" {
#include "ec_commands.h"
#include "overflow.h"
}

bool fp_match_success(int match_result)
{
	return match_result == EC_MKBP_FP_ERR_MATCH_YES ||
	       match_result == EC_MKBP_FP_ERR_MATCH_YES_UPDATED ||
	       match_result == EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED;
}

enum ec_error_list validate_fp_buffer_offset(const uint32_t buffer_size,
					     const uint32_t offset,
					     const uint32_t size)
{
	uint32_t bytes_requested;

	if (check_add_overflow(size, offset, &bytes_requested))
		return EC_ERROR_OVERFLOW;

	if (bytes_requested > buffer_size)
		return EC_ERROR_INVAL;

	return EC_SUCCESS;
}
