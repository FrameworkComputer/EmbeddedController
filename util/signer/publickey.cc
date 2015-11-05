/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <common/publickey.h>

#include <string.h>
#include <string>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#include <common/gnubby.h>

extern bool FLAGS_verbose;

#define VERBOSE(...)  do{if(FLAGS_verbose){fprintf(stderr,  __VA_ARGS__);fflush(stderr);}}while(0)
#define WARN(...)  do{fprintf(stderr,  __VA_ARGS__);}while(0)
#define FATAL(...)  do{fprintf(stderr,  __VA_ARGS__);abort();}while(0)

PublicKey::PublicKey(const std::string& filename) : key_(NULL), publicOnly_(true) {
  EVP_PKEY* pkey = NULL;
  BIO* bio = BIO_new(BIO_s_file());

  OpenSSL_add_all_ciphers();  // needed to decrypt PEM.
  if (BIO_read_filename(bio, filename.c_str()) == 1) {
    pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);

    if (pkey) {
      publicOnly_ = false;
    } else {
      // Try read as public key.
      (void)BIO_reset(bio);
      pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
      if (pkey) {
        VERBOSE("read public key only, assuming gnubby for signing..\n");
      }
    }
  }

  if (!pkey) {
    WARN("loadKey: failed to load RSA key from '%s'\n", filename.c_str());
  }

  BIO_free_all(bio);
  key_ = pkey;
}

PublicKey::~PublicKey() {
  if (key_) {
    EVP_PKEY_free(key_);
    key_ = NULL;
  }
}

bool PublicKey::ok() {
  return key_ != NULL;
}

size_t PublicKey::nwords() {
  RSA* rsa = EVP_PKEY_get1_RSA(key_);
  size_t result = (BN_num_bytes(rsa->n) + 3) / 4;
  RSA_free(rsa);
  return result;
}

uint32_t PublicKey::public_exponent() {
  RSA* rsa = EVP_PKEY_get1_RSA(key_);
  uint32_t result = BN_get_word(rsa->e);
  RSA_free(rsa);
  return result;
}

uint32_t PublicKey::n0inv() {
  RSA* rsa = EVP_PKEY_get1_RSA(key_);
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

void PublicKey::print(const char* tag, size_t nwords, BIGNUM* n) {
  BN_CTX* ctx = BN_CTX_new();
  BIGNUM* N = BN_new();
  BIGNUM* r = BN_new();
  BIGNUM* d = BN_new();
  BIGNUM* rem = BN_new();

  BN_set_bit(r, 32);  // 2^32
  BN_copy(N, n);

  printf("const uint32_t %s[%lu + 1] = {", tag, nwords);
  printf("0x%08x, ", n0inv());
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

void PublicKey::print(const char* tag, size_t nwords,
                      uint8_t* data, size_t len) {
  BIGNUM* n = BN_bin2bn(data, len, NULL);
  print(tag, nwords, n);
  BN_free(n);
}

void PublicKey::print(const char* tag) {
  RSA* rsa = EVP_PKEY_get1_RSA(key_);
  print(tag, rwords(), rsa->n);
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

void PublicKey::modToArray(uint32_t* dst, size_t nwords) {
  RSA* rsa = EVP_PKEY_get1_RSA(key_);
  toArray(dst, nwords, rsa->n);
  RSA_free(rsa);
}

int PublicKey::encrypt(uint8_t* msg, int msglen, uint8_t* out) {
  RSA* rsa = EVP_PKEY_get1_RSA(key_);
  int result =
      RSA_public_encrypt(msglen, msg, out, rsa, RSA_PKCS1_OAEP_PADDING);
  RSA_free(rsa);
  return result;
}

int PublicKey::decrypt(uint8_t* msg, int msglen, uint8_t* out) {
  RSA* rsa = EVP_PKEY_get1_RSA(key_);
  int result =
      RSA_private_decrypt(msglen, msg, out, rsa, RSA_PKCS1_OAEP_PADDING);
  RSA_free(rsa);
  return result;
}


int PublicKey::raw(uint8_t* in, int inlen, BIGNUM** out) {
  RSA* rsa = EVP_PKEY_get1_RSA(key_);
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

  unsigned int tmplen = EVP_PKEY_size(key_);

  ctx = EVP_MD_CTX_create();
  if (!ctx) goto __fail;

  EVP_MD_CTX_init(ctx);
  EVP_DigestInit(ctx, EVP_sha256());
  if (EVP_DigestUpdate(ctx, msg, msglen) != 1) goto __fail;

  sig = (uint8_t*)malloc(tmplen);

  if (publicOnly_) {
    if (nwords() == 64) {
      // 2048 bit public key : gnubby
      fprintf(stderr, "gnubby signing.."); fflush(stderr);

      Gnubby gnubby;
      result = gnubby.sign(ctx, sig, &siglen, key_);
      fprintf(stderr, "Gnubby.sign: %d\n", result);
    } else {
      // other public key : best have signature prefilled
      fprintf(stderr, "WARNING: public key size %lu; assuming preloaded signature\n", nwords());
      fprintf(stderr, "         Likely you are trying to use the real rom key, try the -dev flavor\n");
      fflush(stderr);
      siglen = BN_bn2bin(*output, sig);
      result = 1;
    }
  } else {
    VERBOSE("ossl signing..");
    result = EVP_SignFinal(ctx, sig, &siglen, key_);
    VERBOSE("EVP_SignFinal: %d\n", result);
  }

  if (result != 1) goto __fail;

  tmp = BN_bin2bn(sig, siglen, NULL);

  // compute R*sig mod N
  rsa = EVP_PKEY_get1_RSA(key_);
  if (BN_lshift(tmp, tmp, rwords() * 32) != 1) goto __fail;

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

int PublicKey::writeToGnubby() {
  if (publicOnly_) return -1;

  RSA* rsa = EVP_PKEY_get1_RSA(key_);

  Gnubby gnubby;
  int result = gnubby.write(rsa);

  RSA_free(rsa);

  return result;
}
