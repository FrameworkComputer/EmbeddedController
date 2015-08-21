//
// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#ifndef __EC_UTIL_SIGNER_PUBLICKEY_H
#define __EC_UTIL_SIGNER_PUBLICKEY_H

#include <stddef.h>
#include <inttypes.h>

typedef struct evp_pkey_st EVP_PKEY;
typedef struct bignum_st BIGNUM;

class PublicKey {
 public:
  explicit PublicKey(const char* filename);
  ~PublicKey();

  bool ok();
  size_t nwords();
  uint32_t public_exponent();
  uint32_t n0inv();

  // PKCS1.5 SHA256
  int sign(const void* msg, size_t msglen, BIGNUM** output);

  // PKCS1_OAEP SHA-1, MGF1
  int encrypt(uint8_t* in, int inlen, uint8_t* out);

  // PKCS1_OAEP SHA-1, MGF1
  int decrypt(uint8_t* in, int inlen, uint8_t* out);

  int raw(uint8_t* in, int inlen, BIGNUM** out);

  static void print(const char* tag, size_t nwords, uint8_t* data, size_t len);
  static void print(const char* tag, size_t nwords, BIGNUM* n);
  static void toArray(uint32_t* dst, size_t nwords, BIGNUM* n);

  void print(const char* tag);
  void printAll(const char* tag);
  void toArray(uint32_t* dst);

 private:
  EVP_PKEY* key;

  PublicKey& operator=(const PublicKey& other);
  PublicKey(const PublicKey& other);
};

#endif // __EC_UTIL_SIGNER_PUBLICKEY_H
