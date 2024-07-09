/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Newton's method for sphere fit algorithm */

#ifndef __CROS_EC_NEWTON_FIT_H
#define __CROS_EC_NEWTON_FIT_H

#include "queue.h"
#include "stdbool.h"
#include "vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

struct newton_fit_orientation {
	/** An orientations. */
	fpv3_t orientation;

	/** The number of samples of this orientation. */
	uint8_t nsamples;
};

struct newton_fit {
	/**
	 * Threshold used to detect when two vectors are identical. Measured in
	 * units^2.
	 */
	fp_t nearness_threshold;

	/**
	 * The weight to use for a new data point when computing the mean. When
	 * a new point is considered the same as an existing orientation (via
	 * the nearness_threshold) it will be averaged with the existing
	 * orientation using this weight. Valid range is (0,1).
	 */
	fp_t new_pt_weight;

	/**
	 * The threshold used to determine whether or not to continue iterating
	 * when performing the bias computation.
	 */
	fp_t error_threshold;

	/**
	 * The maximum number of orientations to use, changing this affects the
	 * memory footprint of the algorithm as 3 floats are needed per
	 * orientation.
	 */
	uint32_t max_orientations;

	/**
	 * The maximum number of iterations the algorithm is allowed to run.
	 */
	uint32_t max_iterations;

	/**
	 * The minimum number of samples per orientation to consider the
	 * orientation ready for calculation
	 */
	uint8_t min_orientation_samples;

	/**
	 * Queue of newton_fit_orientation structs.
	 */
	struct queue *orientations;
};

#define NEWTON_FIT(SIZE, NSAMPLES, NEAR_THRES, NEW_PT_WEIGHT, ERROR_THRESHOLD, \
		   MAX_ITERATIONS)                                             \
	((struct newton_fit){                                                  \
		.nearness_threshold = NEAR_THRES,                              \
		.new_pt_weight = NEW_PT_WEIGHT,                                \
		.error_threshold = ERROR_THRESHOLD,                            \
		.max_orientations = SIZE,                                      \
		.max_iterations = MAX_ITERATIONS,                              \
		.min_orientation_samples = NSAMPLES,                           \
		.orientations = (struct queue *)&QUEUE_NULL(                   \
			SIZE, struct newton_fit_orientation),                  \
	})

/**
 * Reset the newton_fit struct's state.
 *
 * @param fit Pointer to the struct.
 */
void newton_fit_reset(struct newton_fit *fit);

/**
 * Add new vector to the struct. The behavior of this depends on the
 * configuration values used when the struct was created. For example:
 * - Samples that are within sqrt(NEAR_THRES) of an existing orientation will
 *   be averaged with the matching orientation entry.
 * - If the new sample isn't near an existing orientation it will only be added
 *   if state->num_orientations < config->num_orientations.
 *
 * @param fit Pointer to the struct.
 * @param x The new samples' X component.
 * @param y The new samples' Y component.
 * @param z The new samples' Z component.
 * @return True if orientations are full and the struct is ready to compute the
 *         bias.
 */
bool newton_fit_accumulate(struct newton_fit *fit, fp_t x, fp_t y, fp_t z);

/**
 * Compute the center/bias and optionally the radius represented by the current
 * struct.
 *
 * @param fit Pointer to the struct.
 * @param bias Pointer to the output bias (this is also the starting bias for
 *             the algorithm.
 * @param radius Optional pointer to write the computed radius into. If NULL,
 *               the calculation will be skipped.
 */
void newton_fit_compute(struct newton_fit *fit, fpv3_t bias, fp_t *radius);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_NEWTON_FIT_H */
