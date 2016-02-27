/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_UTIL_SIGNER_COMMON_SIGNED_HEADER_H
#define __EC_UTIL_SIGNER_COMMON_SIGNED_HEADER_H

#include <assert.h>
#include <string.h>
#include <inttypes.h>

#define FUSE_PADDING 0x55555555  // baked in hw!
#define FUSE_IGNORE 0xa3badaac   // baked in rom!
#define FUSE_MAX 128             // baked in rom!

#define INFO_MAX 128             // baked in rom!
#define INFO_IGNORE 0xaa3c55c3   // baked in rom!

typedef struct SignedHeader {
#ifdef __cplusplus
  SignedHeader() : magic(-1), image_size(0),
                   epoch_(0x1337), major_(0), minor_(0xbabe),
                   p4cl_(0), applysec_(0), config1_(0), err_response_(0), expect_response_(0) {
    memset(signature, 'S', sizeof(signature));
    memset(tag, 'T', sizeof(tag));
    memset(fusemap, 0, sizeof(fusemap));
    memset(infomap, 0, sizeof(infomap));
    memset(_pad, '3', sizeof(_pad));
  }

  void markFuse(uint32_t n) {
    assert(n < FUSE_MAX);
    fusemap[n / 32] |= 1 << (n & 31);
  }

  void markInfo(uint32_t n) {
    assert(n < INFO_MAX);
    infomap[n / 32] |= 1 << (n & 31);
  }

  void print() const {
  }
#endif  // __cplusplus

  uint32_t magic;       // -1 (thanks, boot_sys!)
  uint32_t signature[96];
  uint32_t img_chk_;    // top 32 bit of expected img_hash
  // --------------------- everything below is part of img_hash
  uint32_t tag[7];      // words 0-6 of RWR/FWR
  uint32_t keyid;       // word 7 of RWR
  uint32_t key[96];     // public key to verify signature with
  uint32_t image_size;
  uint32_t ro_base;     // readonly region
  uint32_t ro_max;
  uint32_t rx_base;     // executable region
  uint32_t rx_max;
  uint32_t fusemap[FUSE_MAX / (8 * sizeof(uint32_t))];
  uint32_t infomap[INFO_MAX / (8 * sizeof(uint32_t))];
  uint32_t epoch_;      // word 7 of FWR
  uint32_t major_;      // keyladder count
  uint32_t minor_;
  uint64_t timestamp_;  // time of signing
  uint32_t p4cl_;
  uint32_t applysec_;   // bits to and with FUSE_FW_DEFINED_BROM_APPLYSEC
  uint32_t config1_;    // bits to mesh with FUSE_FW_DEFINED_BROM_CONFIG1
  uint32_t err_response_; // bits to or with FUSE_FW_DEFINED_BROM_ERR_RESPONSE
  uint32_t expect_response_;  // action to take when expectation is violated
  uint32_t _pad[256 - 1 - 96 - 1 - 7 - 1 - 96 - 5*1 - 4 - 4 - 9*1 - 2 - 1];
  uint32_t fuses_chk_;  // top 32 bit of expected fuses hash
  uint32_t info_chk_;   // top 32 bit of expected info hash
} SignedHeader;

#ifdef __cplusplus
static_assert(sizeof(SignedHeader) == 1024, "SignedHeader should be 1024 bytes");
static_assert(offsetof(SignedHeader, info_chk_) == 1020, "SignedHeader should be 1024 bytes");
#endif  // __cplusplus

#endif // __EC_UTIL_SIGNER_COMMON_SIGNED_HEADER_H
