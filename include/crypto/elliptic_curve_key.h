/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Helpers for the boringssl elliptic curve key interface. */

#ifndef __CROS_EC_ELLIPTIC_CURVE_KEY_H
#define __CROS_EC_ELLIPTIC_CURVE_KEY_H

#include "openssl/ec_key.h"
#include "openssl/mem.h"

/**
 * Generate a p256 ECC key.
 * @return key on success, nullptr on failure
 */
bssl::UniquePtr<EC_KEY> generate_elliptic_curve_key();

#endif /* __CROS_EC_ELLIPTIC_CURVE_KEY_H */
