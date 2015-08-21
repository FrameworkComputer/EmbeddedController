//
// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "publickey.h"

#include <string>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

PublicKey::PublicKey(const char* filename) {
  EVP_PKEY* pkey = NULL;
  BIO* bio = BIO_new(BIO_s_file());

  if (BIO_read_filename(bio, filename) == 1) {
    pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
  }

  if (NULL == pkey) {
    fprintf(stderr, "loadKey: failed to load RSA key from '%s'",
            filename);
  }

  BIO_free_all(bio);
  key = pkey;
}

PublicKey::~PublicKey() {
  if (key) {
    EVP_PKEY_free(key);
    key = NULL;
  }
}

bool PublicKey::ok() {
  return key != NULL;
}

size_t PublicKey::nwords() {
  RSA* rsa = EVP_PKEY_get1_RSA(key);
  size_t result = (BN_num_bytes(rsa->n) + 3) / 4;
  RSA_free(rsa);
  return result;
}

uint32_t PublicKey::public_exponent() {
  RSA* rsa = EVP_PKEY_get1_RSA(key);
  uint32_t result = BN_get_word(rsa->e);
  RSA_free(rsa);
  return result;
}

uint32_t PublicKey::n0inv() {
  RSA* rsa = EVP_PKEY_get1_RSA(key);
  BN_CTX* ctx = BN_CTX_new();
  BIGNUM* r = BN_new();
  BIGNUM* rem = BN_new();
  BIGNUM* n0inv = BN_new();
  BN_set_bit(r, 32);  // 2**32
  BN_div(NULL, rem, rsa->n, r, ctx);  // low 32 bit
  BN_mod_inverse(n0inv, rem, r, ctx);

  uint32_t result = 0 - BN_get_word(n0inv);

  BN_free(n0inv);
  BN_free(rem);
  BN_free(r);
  BN_CTX_free(ctx);
  RSA_free(rsa);

  return result;
}

/*static*/
void PublicKey::print(const char* tag, size_t nwords, BIGNUM* n) {
  BN_CTX* ctx = BN_CTX_new();
  BIGNUM* N = BN_new();
  BIGNUM* r = BN_new();
  BIGNUM* d = BN_new();
  BIGNUM* rem = BN_new();

  BN_set_bit(r, 32);  // 2^32
  BN_copy(N, n);

  printf("const uint32_t %s[%lu] = {", tag, nwords);
  for (size_t i = 0; i < nwords; ++i) {
     if (i) printf(", ");
     BN_div(N, rem, N, r, ctx);
     printf("0x%08lx", BN_get_word(rem));
  }
  printf("};\n");

  BN_free(rem);
  BN_free(d);
  BN_free(r);
  BN_free(N);
  BN_CTX_free(ctx);
}

/*static*/
void PublicKey::print(const char* tag, size_t nwords,
                      uint8_t* data, size_t len) {
  BIGNUM* n = BN_bin2bn(data, len, NULL);
  print(tag, nwords, n);
  BN_free(n);
}

void PublicKey::print(const char* tag) {
  RSA* rsa = EVP_PKEY_get1_RSA(key);
  print(tag, nwords(), rsa->n);
  RSA_free(rsa);
}

void PublicKey::printAll(const char* tag) {
  std::string t(tag);
  printf("#define %s_EXP %u\n", tag, public_exponent());
  printf("#define %s_INV 0x%08x\n", tag, n0inv());
  RSA* rsa = EVP_PKEY_get1_RSA(key);
  print((t + "_MOD").c_str(), nwords(), rsa->n);

  BN_CTX* ctx = BN_CTX_new();
  BIGNUM* RR = BN_new();
  BIGNUM* rem = BN_new();
  BIGNUM* quot = BN_new();
  BN_set_bit(RR, nwords() * 32 * 2);
  BN_div(quot, rem, RR, rsa->n, ctx);

  print((t + "_RR").c_str(), nwords(), rem);

  BN_free(quot);
  BN_free(rem);
  BN_free(RR);
  BN_CTX_free(ctx);

  RSA_free(rsa);
}

/*static*/
void PublicKey::toArray(uint32_t* dst, size_t nwords, BIGNUM* n) {
  BN_CTX* ctx = BN_CTX_new();
  BIGNUM* N = BN_new();
  BIGNUM* r = BN_new();
  BIGNUM* d = BN_new();
  BIGNUM* rem = BN_new();

  BN_set_bit(r, 32);  // 2^32
  BN_copy(N, n);

  for (size_t i = 0; i < nwords; ++i) {
     BN_div(N, rem, N, r, ctx);
     *dst++ = BN_get_word(rem);
  }

  BN_free(rem);
  BN_free(d);
  BN_free(r);
  BN_free(N);
  BN_CTX_free(ctx);
}

int PublicKey::encrypt(uint8_t* msg, int msglen, uint8_t* out) {
  RSA* rsa = EVP_PKEY_get1_RSA(key);
  int result =
      RSA_public_encrypt(msglen, msg, out, rsa, RSA_PKCS1_OAEP_PADDING);
  RSA_free(rsa);
  return result;
}

int PublicKey::decrypt(uint8_t* msg, int msglen, uint8_t* out) {
  RSA* rsa = EVP_PKEY_get1_RSA(key);
  int result =
      RSA_private_decrypt(msglen, msg, out, rsa, RSA_PKCS1_OAEP_PADDING);
  RSA_free(rsa);
  return result;
}


int PublicKey::raw(uint8_t* in, int inlen, BIGNUM** out) {
  RSA* rsa = EVP_PKEY_get1_RSA(key);
  BN_CTX* ctx = BN_CTX_new();
  BIGNUM* m = BN_new();
  BIGNUM* r = BN_new();
  BN_bin2bn(in, inlen, m);
  int result = BN_mod_exp(r, m, rsa->d, rsa->n, ctx);
  if (result == 1) {
    *out = BN_dup(r);
  }
  BN_free(r);
  BN_free(m);
  BN_CTX_free(ctx);
  RSA_free(rsa);
  return result;
}

// Sign message.
// Produces signature * R mod N (Montgomery format).
// Returns 1 on success.
int PublicKey::sign(const void* msg, size_t msglen, BIGNUM** output) {
  int result = 0;
  EVP_MD_CTX* ctx = NULL;
  BN_CTX* bnctx = NULL;
  BIGNUM* tmp = NULL;
  RSA* rsa = NULL;
  uint8_t* sig = NULL;
  unsigned int siglen = 0;

  unsigned int tmplen = EVP_PKEY_size(key);

  ctx = EVP_MD_CTX_create();
  if (!ctx) goto __fail;

  EVP_MD_CTX_init(ctx);
  EVP_DigestInit(ctx, EVP_sha256());
  if (EVP_DigestUpdate(ctx, msg, msglen) != 1) goto __fail;

  sig = (uint8_t*)malloc(tmplen);
  result = EVP_SignFinal(ctx, sig, &siglen, key);
  if (result != 1) goto __fail;

  tmp = BN_bin2bn(sig, siglen, NULL);

  // compute R*sig mod N
  rsa = EVP_PKEY_get1_RSA(key);
  if (BN_lshift(tmp, tmp, nwords() * 32) != 1) goto __fail;

  bnctx = BN_CTX_new();
  if (BN_mod(tmp, tmp, rsa->n, bnctx) != 1) goto __fail;
  *output = BN_dup(tmp);

__fail:
  if (tmp) BN_free(tmp);
  if (rsa) RSA_free(rsa);
  if (sig) free(sig);
  if (ctx) EVP_MD_CTX_destroy(ctx);
  if (bnctx) BN_CTX_free(bnctx);

  return result;
}
