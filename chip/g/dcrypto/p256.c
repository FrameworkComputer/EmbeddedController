/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"

#include "cryptoc/p256.h"
#include "cryptoc/util.h"

static const p256_int p256_one = P256_ONE;

/*
 * Key selection based on FIPS-186-4, section B.4.2 (Key Pair
 * Generation by Testing Candidates).
 */
int DCRYPTO_p256_key_from_bytes(p256_int *x, p256_int *y, p256_int *d,
				const uint8_t key_bytes[P256_NBYTES])
{
	p256_int key;

	p256_from_bin(key_bytes, &key);
	if (p256_cmp(&SECP256r1_nMin2, &key) < 0)
		return 0;
	p256_add(&key, &p256_one, d);
	always_memset(&key, 0, sizeof(key));
	if (x == NULL || y == NULL)
		return 1;
	return dcrypto_p256_base_point_mul(d, x, y);
}
