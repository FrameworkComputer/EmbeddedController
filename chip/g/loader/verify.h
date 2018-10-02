/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_LOADER_VERIFY_H
#define __EC_CHIP_G_LOADER_VERIFY_H

/*
 * Verify a RSA PKCS1.5 signature against an expected sha256. Unlocks for
 * execution upon success.
 */
void LOADERKEY_verify(const uint32_t *key,
		      const uint32_t *signature, const uint32_t *sha256);

#endif  /* __EC_CHIP_G_LOADER_VERIFY_H */
