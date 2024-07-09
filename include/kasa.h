/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kasa sphere fit algorithm */

#ifndef __CROS_EC_KASA_H
#define __CROS_EC_KASA_H

#include "vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

struct kasa_fit {
	fp_t acc_x, acc_y, acc_z, acc_w;
	fp_t acc_xx, acc_xy, acc_xz, acc_xw;
	fp_t acc_yy, acc_yz, acc_yw;
	fp_t acc_zz, acc_zw;
	uint32_t nsamples;
};

/**
 * Resets the kasa_fit data structure (sets all variables to zero).
 *
 * @param kasa Pointer to the struct that should be reset.
 */
void kasa_reset(struct kasa_fit *kasa);

/**
 * Add a new sample to the kasa_fit structure.
 *
 * @param x The X component of the new sample.
 * @param y The Y component of the new sample.
 * @param z the Z component of the new sample.
 */
void kasa_accumulate(struct kasa_fit *kasa, fp_t x, fp_t y, fp_t z);

/**
 * Compute the current center/radius from the kasa_fit structure.
 *
 * @param kasa Pointer to the struct that should be used for the calculation.
 * @param bias Pointer to the start of a fp_t[3] to save the computed center.
 * @param radius Pointer to a fp_t that will hold the computed radius.
 */
void kasa_compute(struct kasa_fit *kasa, fpv3_t bias, fp_t *radius);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_KASA_H */
