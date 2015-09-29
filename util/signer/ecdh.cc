/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <common/ecdh.h>

#include <openssl/obj_mac.h>
#include <openssl/sha.h>

ECDH::ECDH() {
  key_ = EC_KEY_new();
  group_ = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
  EC_KEY_set_group(key_, group_);
  EC_KEY_generate_key(key_);
}

ECDH::~ECDH() {
  EC_GROUP_free(group_);
  EC_KEY_free(key_);
}

void ECDH::get_point(void* dst) {
  EC_POINT_point2oct(group_, EC_KEY_get0_public_key(key_),
                     POINT_CONVERSION_UNCOMPRESSED,
                     reinterpret_cast<unsigned char*>(dst), 65, 0);
}

void ECDH::compute_secret(const void* in, void* out) {
  EC_POINT* b = EC_POINT_new(group_);
  EC_POINT* x = EC_POINT_new(group_);

  EC_POINT_oct2point(group_, b, reinterpret_cast<const unsigned char*>(in), 65, 0);
  EC_POINT_mul(group_, x, 0, b, EC_KEY_get0_private_key(key_), 0);

  unsigned char xbytes[65];
  EC_POINT_point2oct(group_, x,
                     POINT_CONVERSION_UNCOMPRESSED,
                     xbytes, sizeof(xbytes), 0);

  SHA256_CTX sha;
  SHA256_Init(&sha);
  SHA256_Update(&sha, xbytes + 1, 32);  // x coordinate only
  SHA256_Final(reinterpret_cast<unsigned char*>(out), &sha);

  EC_POINT_free(x);
  EC_POINT_free(b);
}
