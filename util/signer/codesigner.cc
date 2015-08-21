//
// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include <iostream>
#include <fstream>
#include <sstream>
#include <ios>
#include <string>


#include <openssl/pem.h>



#include <signed_header.h>
#include <publickey.h>

using namespace std;
static uint8_t *mem; // this is where the file to sign is loaded
// Size of the file to be signed (including 1Kb signature).
static size_t flat_size;


int LoadFlatFile(const char *name, size_t header_size)
{
  ifstream::pos_type fileSize;
  ifstream FlatFile (name, ios::in | ios::binary | ios::ate);

  if(!FlatFile.is_open()) {
    fprintf(stderr, "failed to open %s\n", name);
    return -1;
  }

  flat_size = FlatFile.tellg();

  mem = new uint8_t[flat_size];
  FlatFile.seekg(0, ios::beg);
  if(!FlatFile.read((char *)mem, flat_size)) {
    fprintf(stderr, "failed to read file %s\n", name);
    return -1;
  }
  FlatFile.close();

  // verify that there is enough room at the bottom
  for (size_t i = 0; i < header_size; i++)
    if (mem[i]) {
      fprintf(stderr, "nonzero value at offset %zd\n", i);
      return -1;
    }

  return 0;
}

int SaveSignedFile(const char *name)
{
  FILE* fp = fopen(name, "wb");

  if (!fp) {
    fprintf(stderr, "failed to open file '%s': %s\n", name, strerror(errno));
    return -1;
  }
  if (fwrite(mem, 1, flat_size, fp) != flat_size) {
    fprintf(stderr, "failed to write %zd bytes to '%s': %s\n",
            flat_size, name, strerror(errno));
    return -1;
  }
  fclose(fp);

  return 0;
}

// Sing the previously read file. Return zero on success, nonzero on failure.
static int sign(PublicKey& key, const SignedHeader* input_hdr) {
  BIGNUM* sig = NULL;
  SignedHeader* hdr = (SignedHeader*)(&mem[0]);
  int result;

  memcpy(hdr, input_hdr, sizeof(SignedHeader));

  result = key.sign(&hdr->tag, flat_size - offsetof(SignedHeader, tag), &sig);

  if (result == 1) {
    hdr->image_size = flat_size;
    size_t nwords = key.nwords();
    key.toArray(hdr->signature, nwords, sig);
  } else {
    fprintf(stderr, "ossl_sign:%d\n", result);
  }

  if (sig)
    BN_free(sig);

  return result != 1;
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s pem-file [hexfile|flatfile]\n", argv[0]);
    exit(1);
  }
  const char* arg = argv[2];
  PublicKey key(argv[1]);

  if (!key.ok()) return -1;

  SignedHeader hdr;

  // Load input file
  if (LoadFlatFile(arg, sizeof(hdr)))
    return -2;

  if (sign(key, &hdr))
      return -3;

  if (SaveSignedFile((std::string(arg) + std::string(".signed")).c_str()))
    return -4;

  return 0;
}
