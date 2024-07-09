/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Low pass filter for on die temperature
 */
#ifndef __CROS_EC_TEMPERATURE_FILTER_H
#define __CROS_EC_TEMPERATURE_FILTER_H

#include "util.h"
#include "math_util.h"

#define Q_SCALE 14
/* scale input up to improve filter performance */
#define IN_SCALE 7

struct biquad {
	/* State values x[n-1], x[n-2], y[n-1], y[n-2] */
	int32_t state[4];
	const int32_t *coeff;
};

void thermal_filter_reset(struct biquad *filter);

int thermal_filter_update(struct biquad *filter, int value);

int thermal_filter_get(struct biquad *filter);

#endif /* __CROS_EC_TEMPERATURE_FILTER_H */
