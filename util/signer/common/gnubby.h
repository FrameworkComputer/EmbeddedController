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

class Gnubby {
 public:
  Gnubby();
  ~Gnubby();

  bool ok() const { return handle_ != NULL; }

  int Sign(EVP_MD_CTX* ctx, uint8_t* signature, uint32_t* siglen, EVP_PKEY* key);

 private:
  int send_to_device(uint8_t instruction,
                     const uint8_t* payload,
                     size_t length);

  int receive_from_device(uint8_t* dest, size_t length);

  libusb_context* ctx_;
  libusb_device_handle* handle_;
};

#endif // __EC_UTIL_SIGNER_COMMON_GNUBBY_H
