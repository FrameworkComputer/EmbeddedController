/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_UTIL_SIGNER_COMMON_ECDH_H
#define __EC_UTIL_SIGNER_COMMON_ECDH_H

#include <openssl/ec.h>

class ECDH {
 private:
  EC_KEY* key_;
  EC_GROUP* group_;
 public:
  ECDH();
  ~ECDH();

  void get_point(void* dst);

  // Computes SHA256 of x-coordinate.
  void compute_secret(const void* other, void* secret);
};

#endif // __EC_UTIL_SIGNER_COMMON_ECDH_H
