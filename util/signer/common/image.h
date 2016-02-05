/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_UTIL_SIGNER_COMMON_IMAGE_H
#define __EC_UTIL_SIGNER_COMMON_IMAGE_H

#include <stdio.h>
#include <stddef.h>
#include <inttypes.h>

#include <string>

class PublicKey;
struct SignedHeader;

class Image {
 public:
  Image();
  ~Image();

  void randomize();

  bool fromIntelHex(const std::string& filename, bool withSignature = true);
  bool fromElf(const std::string& filename);

  bool sign(PublicKey& key,
            const SignedHeader* hdr,
            const uint32_t fuses[],
            const uint32_t info[],
            const std::string& hashesFilename);
  void generate(const std::string& outputFilename, bool hex_output) const;


  bool ok() const { return success_; }
  const uint8_t* code() const { return mem_; }
  size_t size() const { return high_ - base_; }
  int base() const { return base_; }
  int ro_base() const { return ro_base_; }
  int rx_base() const { return rx_base_; }
  int ro_max() const { return ro_max_; }
  int rx_max() const { return rx_max_; }

  void fillPattern(uint32_t);
  void fillRandom();

 private:
  void toIntelHex(FILE *fout) const;
  int nibble(char n);
  int parseByte(char** p);
  int parseWord(char** p);
  void store(int adr, int v);

  bool success_;
  uint8_t mem_[8*64*1024];
  int low_, high_, base_;
  size_t ro_base_, rx_base_;
  size_t ro_max_, rx_max_;
};

#endif // __EC_UTIL_SIGNER_COMMON_IMAGE_H
