//
// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#ifndef __EC_UTIL_SIGNER_SIGNED_HEADER_H
#define __EC_UTIL_SIGNER_SIGNED_HEADER_H

#include <string.h>
#include <inttypes.h>

typedef struct SignedHeader {
  SignedHeader() : magic(-1), image_size(0) {
    memset(signature, 'S', sizeof(signature));
    memset(tag, 'T', sizeof(tag));
    memset(fusemap, 0, sizeof(fusemap));
    memset(_pad, -1, sizeof(_pad));
  }

  uint32_t magic;  // -1
  uint32_t image_size;  // != -1
  uint32_t signature[96];
  uint32_t tag[8];
  uint32_t fusemap[32];  // 1024 bits
  uint32_t _pad[256 - 1 - 1 - 96 - 8 - 32];
} SignedHeader;
static_assert(sizeof(SignedHeader) == 1024, "SignedHeader should be 1024 bytes");

#endif // __EC_UTIL_SIGNER_SIGNED_HEADER_H
