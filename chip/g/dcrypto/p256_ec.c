/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"

#include <stdint.h>

#include "cryptoc/p256.h"

/* p256_base_point_mul sets {out_x,out_y} = nG, where n is < the
 * order of the group. */
int DCRYPTO_p256_base_point_mul(p256_int *out_x, p256_int *out_y,
				const p256_int *n)
{
	if (p256_is_zero(n) != 0) {
		p256_clear(out_x);
		p256_clear(out_y);
		return 0;
	}

	return dcrypto_p256_base_point_mul(n, out_x, out_y);
}

/* DCRYPTO_p256_point_mul sets {out_x,out_y} = n*{in_x,in_y}, where n is <
 * the order of the group. */
int DCRYPTO_p256_point_mul(p256_int *out_x, p256_int *out_y,
			const p256_int *n, const p256_int *in_x,
			const p256_int *in_y)
{
	if (p256_is_zero(n) != 0) {
		p256_clear(out_x);
		p256_clear(out_y);
		return 0;
	}

	return dcrypto_p256_point_mul(n, in_x, in_y, out_x, out_y);
}
