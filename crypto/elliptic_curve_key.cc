/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "crypto/elliptic_curve_key.h"
#include "openssl/ec_key.h"
#include "openssl/mem.h"
#include "openssl/obj_mac.h"

bssl::UniquePtr<EC_KEY> generate_elliptic_curve_key()
{
	bssl::UniquePtr<EC_KEY> key(
		EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
	if (key == nullptr) {
		return nullptr;
	}

	if (EC_KEY_generate_key(key.get()) != 1) {
		return nullptr;
	}

	return key;
}
