/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <common/aes.h>

#include <string.h>

#include <openssl/aes.h>
#include <openssl/cmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

AES::AES() {}
AES::~AES() {
  memset(key_, 0, sizeof(key_));
}

void AES::set_key(const void* key) {
  memcpy(key_, key, sizeof(key_));
}

void AES::decrypt_block(const void* in, void* out) {
  AES_KEY aes;
  AES_set_decrypt_key(key_, sizeof(key_) * 8, &aes);
  AES_decrypt(reinterpret_cast<const unsigned char*>(in),
              reinterpret_cast<unsigned char*>(out), &aes);
}

void AES::encrypt_block(const void* in, void* out) {
  AES_KEY aes;
  AES_set_encrypt_key(key_, sizeof(key_) * 8, &aes);
  AES_encrypt(reinterpret_cast<const unsigned char*>(in),
              reinterpret_cast<unsigned char*>(out), &aes);
}

void AES::cmac(const void* in, size_t in_len, void* out) {
  unsigned char digest[SHA256_DIGEST_LENGTH];

  SHA256_CTX sha;
  SHA256_Init(&sha);
  SHA256_Update(&sha, reinterpret_cast<const unsigned char*>(in), in_len);
  SHA256_Final(digest, &sha);

  CMAC_CTX* cmac = CMAC_CTX_new();
  CMAC_Init(cmac, key_, sizeof(key_), EVP_aes_128_cbc(), 0);
  CMAC_Update(cmac, digest, sizeof(digest));
  size_t out_len;
  CMAC_Final(cmac, reinterpret_cast<unsigned char*>(out), &out_len);
  CMAC_CTX_free(cmac);
}
