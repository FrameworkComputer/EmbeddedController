/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_UTIL_SIGNER_COMMON_GNUBBY_H
#define __EC_UTIL_SIGNER_COMMON_GNUBBY_H

#include <stddef.h>
#include <inttypes.h>

#include <libusb-1.0/libusb.h>

typedef struct env_md_ctx_st EVP_MD_CTX;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct rsa_st RSA;
typedef struct bignum_st BIGNUM;

class Gnubby {
 public:
  Gnubby();
  ~Gnubby();

  bool ok() const { return handle_ != NULL; }

  int sign(EVP_MD_CTX* ctx, uint8_t* signature, uint32_t* siglen, EVP_PKEY* key);

  int write(RSA* rsa);

 private:
  int send_to_device(uint8_t instruction,
                     const uint8_t* payload,
                     size_t length);

  int receive_from_device(uint8_t* dest, size_t length);

  int write_bn(uint8_t ins, BIGNUM* n, size_t length);
  int doSign(EVP_MD_CTX* ctx, uint8_t* padded_req,  uint8_t* signature,
             uint32_t* siglen, EVP_PKEY* key);
  // Open a gnubby, unspecified selection made when multiple plugged in.
  int open();

  libusb_context* ctx_;
  libusb_device_handle* handle_;
};

#endif // __EC_UTIL_SIGNER_COMMON_GNUBBY_H
