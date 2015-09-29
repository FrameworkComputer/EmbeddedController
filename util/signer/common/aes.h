/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_UTIL_SIGNER_COMMON_AES_H
#define __EC_UTIL_SIGNER_COMMON_AES_H

#include <stddef.h>
#include <inttypes.h>

class AES {
 private:
  unsigned char key_[16];
 public:
  AES();
  ~AES();

  void set_key(const void* key);
  void decrypt_block(const void* in, void* out);
  void encrypt_block(const void* in, void* out);
  void cmac(const void* in, size_t in_len, void* out);
};

#endif // __EC_UTIL_SIGNER_COMMON_AES_H
