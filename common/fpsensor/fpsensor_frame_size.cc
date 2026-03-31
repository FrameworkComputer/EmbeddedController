/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_frame_size.h"

#include <stddef.h>

#include <vector>

void FpFrameSizeCache::populate_cache(uint32_t max_frame_size_bytes)
{
	const size_t buffer_size =
		sizeof(struct ec_response_fp_info_v3) +
		sizeof(struct fp_image_frame_params_v2) * frame_sizes_.size();

	std::vector<uint8_t> buffer(buffer_size);
	auto *info = reinterpret_cast<struct ec_response_fp_info_v3 *>(
		buffer.data());

	if (fp_sensor_get_info(info, buffer.size()) < 0) {
		CPRINTF("Error: Failed to get fingerprint sensor info.");
		return;
	}

	const uint8_t num_types = info->sensor_info.num_capture_types;

	if (num_types > frame_sizes_.size()) {
		CPRINTF("ERROR - EC returned %u types, max supported is %zu.",
			num_types, frame_sizes_.size());
		return;
	}

	auto *params = info->image_frame_params;

	for (uint8_t i = 0; i < num_types; ++i) {
		const uint8_t type = params[i].fp_capture_type;
		const uint32_t size = params[i].frame_size;

		if (size > max_frame_size_bytes) {
			CPRINTF("Error: Type %u frame size (%u) exceeds max allowed (%u).",
				type, size, max_frame_size_bytes);
			goto error_exit;
		}

		if (type >= frame_sizes_.size()) {
			CPRINTF("ERROR: Invalid fp_capture_type %u received from EC, max "
				"supported is %zu.",
				type, frame_sizes_.size());
			goto error_exit;
		}

		if (size == 0) {
			CPRINTF("ERROR: Invalid 0 size for capture type %u.",
				type);
			goto error_exit;
		}

		frame_sizes_[type] = size;
	}

	return;

error_exit:
	/* Invalidate cache. */
	frame_sizes_.fill(0);
	return;
}

uint32_t
FpFrameSizeCache::get_frame_size(enum fp_capture_type capture_type) const
{
	if (static_cast<size_t>(capture_type) >= frame_sizes_.size() ||
	    capture_type < 0) {
		CPRINTF("Error: Invalid fp_capture_type %d requested (max: %zu), "
			"returning size 0.",
			capture_type, frame_sizes_.size());
		return 0;
	}

	if (frame_sizes_[capture_type] == 0) {
		CPRINTF("Error: FpFrameSizeCache is uninitialized or capture type %u"
			" is invalid, returning size 0.",
			capture_type);
	}

	return frame_sizes_[capture_type];
}
