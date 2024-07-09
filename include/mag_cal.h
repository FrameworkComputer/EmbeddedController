/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Online magnetometer calibration */

#ifndef __CROS_EC_MAG_CAL_H
#define __CROS_EC_MAG_CAL_H

#include "kasa.h"
#include "mat44.h"
#include "math_util.h"
#include "vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAG_CAL_MAX_SAMPLES 0xffff
#define MAG_CAL_MIN_BATCH_WINDOW_US (2 * SECOND)
#define MAG_CAL_MIN_BATCH_SIZE 50 /* samples */

struct mag_cal_t {
	struct kasa_fit kasa_fit;
	fp_t radius;

	intv3_t bias;

	/* number of samples needed to calibrate */
	uint16_t batch_size;
};

void init_mag_cal(struct mag_cal_t *moc);

/**
 * Update the magnetometer calibration structure and possibly compute the new
 * bias.
 *
 * @param moc Pointer to the magnetometer struct to update.
 * @param v   The new data.
 * @return    1 if a new calibration value is available, 0 otherwise.
 */
int mag_cal_update(struct mag_cal_t *moc, const intv3_t v);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_MAG_CAL_H */
