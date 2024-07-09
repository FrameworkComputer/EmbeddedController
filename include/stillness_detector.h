/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_STILLNESS_DETECTOR_H
#define __CROS_EC_STILLNESS_DETECTOR_H

#include "common.h"
#include "math_util.h"
#include "stdbool.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct still_det {
	/** Variance threshold for the stillness confidence score. [units]^2 */
	fp_t var_threshold;

	/** The minimum window duration to consider a still sample. */
	uint32_t min_batch_window;

	/** The maximum window duration to consider a still sample. */
	uint32_t max_batch_window;

	/**
	 * The minimum number of samples in a window to consider a still sample.
	 */
	uint16_t min_batch_size;

	/** The timestamp of the first sample in the current batch. */
	uint32_t window_start_time;

	/** The number of samples in the current batch. */
	uint16_t num_samples;

	/** Accumulators used for calculating stillness. */
	fp_t acc_x, acc_y, acc_z, acc_xx, acc_yy, acc_zz, mean_x, mean_y,
		mean_z;
};

#define STILL_DET(VAR_THRES, MIN_BATCH_WIN, MAX_BATCH_WIN, MIN_BATCH_SIZE) \
	((struct still_det){                                               \
		.var_threshold = VAR_THRES,                                \
		.min_batch_window = MIN_BATCH_WIN,                         \
		.max_batch_window = MAX_BATCH_WIN,                         \
		.min_batch_size = MIN_BATCH_SIZE,                          \
		.window_start_time = 0,                                    \
		.acc_x = 0.0f,                                             \
		.acc_y = 0.0f,                                             \
		.acc_z = 0.0f,                                             \
		.acc_xx = 0.0f,                                            \
		.acc_yy = 0.0f,                                            \
		.acc_zz = 0.0f,                                            \
		.mean_x = 0.0f,                                            \
		.mean_y = 0.0f,                                            \
		.mean_z = 0.0f,                                            \
	})

/**
 * Update a stillness detector with a new sample.
 *
 * @param sample_time The timestamp of the sample to add.
 * @param x The x component of the sample to add.
 * @param y The y component of the sample to add.
 * @param z The z component of the sample to add.
 * @return True if the sample triggered a complete batch and mean_* are now
 *         valid.
 */
bool still_det_update(struct still_det *still_det, uint32_t sample_time, fp_t x,
		      fp_t y, fp_t z);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_STILLNESS_DETECTOR_H */
