/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_UTIL_SIGNER_COMMON_SIGNED_HEADER_H
#define __EC_UTIL_SIGNER_COMMON_SIGNED_HEADER_H

#include <assert.h>
#include <string.h>
#include <inttypes.h>

#define FUSE_PADDING 0x55555555
#define FUSE_IGNORE 0xaaaaaaaa
#define FUSE_MAX 160

#define INFO_MAX 128
#define INFO_IGNORE 0xaa3c55c3

typedef struct SignedHeader {
#ifdef __cplusplus
  SignedHeader() : magic(-1), image_size(0) {
    memset(signature, 'S', sizeof(signature));
    memset(tag, 'T', sizeof(tag));
    memset(fusemap, 0, sizeof(fusemap));
    memset(infomap, 0, sizeof(infomap));
    memset(_pad, 0xdd, sizeof(_pad));
  }

  void markFuse(uint32_t n) {
    assert(n < FUSE_MAX);
    fusemap[n / 32] |= 1 << (n & 31);
  }

  void markInfo(uint32_t n) {
    assert(n < INFO_MAX);
    infomap[n / 32] |= 1 << (n & 31);
  }
#endif  // __cplusplus

  uint32_t magic;  // -1
  uint32_t signature[96];
  uint32_t tag[7];
  uint32_t keyid;
  uint32_t image_size;
  uint32_t ro_base;
  uint32_t ro_max;
  uint32_t rx_base;
  uint32_t rx_max;
  uint32_t fusemap[FUSE_MAX / (8 * sizeof(uint32_t))];
  uint32_t infomap[INFO_MAX / (8 * sizeof(uint32_t))];
  uint32_t _pad[256 - 1 - 96 - 7 - 6*1 - 5 - 4];
} SignedHeader;

#ifdef __cplusplus
static_assert(sizeof(SignedHeader) == 1024, "SignedHeader should be 1024 bytes");
#endif  // __cplusplus

#endif // __EC_UTIL_SIGNER_COMMON_SIGNED_HEADER_H
