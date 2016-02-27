/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <common/image.h>

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <gelf.h>
#include <libelf.h>

#include <common/publickey.h>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include <common/signed_header.h>

#include <string>

using namespace std;

extern bool FLAGS_verbose;

#define VERBOSE(...)                                 \
  do {                                               \
    if (FLAGS_verbose) fprintf(stderr, __VA_ARGS__); \
  } while (0)
#define WARN(...)                 \
  do {                            \
    fprintf(stderr, __VA_ARGS__); \
  } while (0)
#define FATAL(...)                \
  do {                            \
    fprintf(stderr, __VA_ARGS__); \
    abort();                      \
  } while (0)

static const int FLASH_START = 0x4000;
static const int FLASH_END = FLASH_START + 512 * 1024;

Image::Image()
    : success_(true),
      low_(FLASH_END - FLASH_START),
      high_(0),
      base_(0),
      ro_base_(FLASH_END * 16),
      rx_base_(FLASH_END * 16),
      ro_max_(0),
      rx_max_(0) {
  memset(mem_, 0xff, sizeof(mem_));  // default memory content
}

void Image::randomize() {
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    read(fd, mem_, sizeof(mem_));
    close(fd);
  }
}

bool Image::fromElf(const string& filename) {
  Elf* elf = NULL;
  Elf_Scn* scn = NULL;
  GElf_Shdr shdr;
  GElf_Phdr phdr;

  char* base_ptr = NULL;
  struct stat elf_stats;

  bool result = false;

  int fd;

  if ((fd = open(filename.c_str(), O_RDONLY)) < 0) {
    WARN("failed to open '%s'\n", filename.c_str());
    goto fail;
  }
  if ((fstat(fd, &elf_stats)) < 0) {
    WARN("cannot stat '%s'\n", filename.c_str());
    goto fail;
  }

  //  printf("Elf filesize: %lu\n", elf_stats.st_size);

  if ((base_ptr = (char*)malloc(elf_stats.st_size)) == NULL) {
    WARN("cannot malloc %lu\n", elf_stats.st_size);
    goto fail;
  }

  if (read(fd, base_ptr, elf_stats.st_size) < elf_stats.st_size) {
    WARN("cannot read from '%s'\n", filename.c_str());
    goto fail;
  }

  // Sniff content for sanity
  if (*(uint32_t*)base_ptr != 0x464c457f) {
    //    WARN("'%s' is not elf file\n", filename);
    goto fail;
  }

  if (elf_version(EV_CURRENT) == EV_NONE) {
    WARN("Warning: elf library is out of date!\n");
  }

  elf = elf_begin(fd, ELF_C_READ, NULL);

  // Infer minimal rx segment from section headers.
  while ((scn = elf_nextscn(elf, scn)) != 0) {
    gelf_getshdr(scn, &shdr);

    VERBOSE("type %08x; flags %08lx ", shdr.sh_type, shdr.sh_flags);
    VERBOSE("%08lx(@%08lx)[%08lx] align %lu\n", shdr.sh_addr, shdr.sh_offset,
            shdr.sh_size, shdr.sh_addralign);

    // Ignore sections that are not alloc
    if (!(shdr.sh_flags & SHF_ALLOC)) {
      continue;
    }

    // Ignore sections that are not exec
    if (!(shdr.sh_flags & SHF_EXECINSTR)) {
      continue;
    }

    // Ignore sections outside our flash range
    if (shdr.sh_addr < FLASH_START * 16 ||
        shdr.sh_addr + shdr.sh_size >= FLASH_END * 16) {
      continue;
    }

    // Track rx boundaries
    if (shdr.sh_addr < rx_base_) {
      rx_base_ = shdr.sh_addr;
    }
    if (shdr.sh_addr + shdr.sh_size > rx_max_) {
      rx_max_ = shdr.sh_addr + shdr.sh_size;
    }
  }

  // Load image per program headers and track total ro segment
  for (int index = 0; gelf_getphdr(elf, index, &phdr); ++index) {
    VERBOSE("phdr %08lx(@%08lx) [%08lx/%08lx]", phdr.p_vaddr, phdr.p_paddr,
            phdr.p_filesz, phdr.p_memsz);

    // Ignore sections outside our flash range
    if (phdr.p_paddr < FLASH_START * 16 ||
        phdr.p_paddr + phdr.p_filesz >= FLASH_END * 16) {
      VERBOSE(" (outside flash, skipped)\n");
      continue;
    }

    // Ignore p_offset 0, which ELF hdr; cannot be legit.
    if (phdr.p_offset == 0) {
      VERBOSE(" (offset 0, ignoring)\n");
      continue;
    }

    VERBOSE("\n");

    // Track ro boundaries
    if (phdr.p_paddr < ro_base_) {
      ro_base_ = phdr.p_paddr;
    }
    if (phdr.p_paddr + phdr.p_filesz > ro_max_) {
      ro_max_ = phdr.p_paddr + phdr.p_filesz;
    }

    // Copy data into image
    for (size_t n = 0; n < phdr.p_filesz; ++n) {
      store(phdr.p_paddr + n - FLASH_START * 16, base_ptr[phdr.p_offset + n]);
    }
  }

  low_ &= ~2047;
  base_ = low_;

  // Set ro_base to start, so app can read its own header.
  ro_base_ = base_ + FLASH_START * 16;
  // Set rx_base to just past header, where interrupt vectors are,
  // since fetching a vector gets done on the I bus.
  rx_base_ = ro_base_ + sizeof(SignedHeader);

  high_ = ((high_ + 2047) / 2048) * 2048;  // Round image to multiple of 2K.

  VERBOSE("Rounded image size %lu\n", size());
  VERBOSE("ro_base %08lx..%08lx\n", ro_base_, ro_max_);
  VERBOSE("rx_base %08lx..%08lx\n", rx_base_, rx_max_);

  result = true;

fail:
  if (elf) elf_end(elf);
  if (base_ptr) free(base_ptr);
  if (fd >= 0) close(fd);

  return result;
}

bool Image::fromIntelHex(const string& filename, bool withSignature) {
  bool isRam = false;
  int seg = 0;

  FILE* fp = fopen(filename.c_str(), "rt");
  if (fp != NULL) {
    char line[BUFSIZ];
    while (fgets(line, sizeof(line), fp)) {
      if (strchr(line, '\r')) *strchr(line, '\r') = 0;
      if (strchr(line, '\n')) *strchr(line, '\n') = 0;
      if (line[0] != ':') continue;  // assume comment line
      if (strlen(line) < 9) {
        WARN("short record %s", line);
        success_ = false;
        continue;
      }
      if (line[7] != '0') {
        WARN("unknown record type %s", line);
        success_ = false;
      } else
        switch (line[8]) {
          case '1': {  // 01 eof
          } break;
          case '2': {  // 02 segment
            if (!strncmp(line, ":02000002", 9)) {
              char* p = line + 9;
              int s = parseWord(&p);
              if (s != 0x1000) {
                if (s >= FLASH_START && s <= FLASH_END) {
                  seg = s - FLASH_START;
                  // WARN("at segment %04x\n", seg);
                } else {
                  WARN("data should in range %x-%x: %s\n", FLASH_START,
                       FLASH_END, line);
                  success_ = false;
                }
              }
            }
            isRam = !strcmp(line, ":020000021000EC");
          } break;
          case '0': {  // 00 data
            char* p = line + 1;
            int len = parseByte(&p);
            int adr = parseWord(&p);
            parseByte(&p);
            while (len--) {
              if (isRam) {
                int v = parseByte(&p);
                if (v != 0) {
                  WARN("WARNING: non-zero RAM byte %02x at %04x\n", v, adr);
                }
                ++adr;
              } else {
                store((seg * 16) + adr++, parseByte(&p));
              }
            }
          } break;
          case '3': {  // 03 entry point
          } break;
          default: {
            WARN("unknown record type %s", line);
            success_ = false;
          } break;
        }
    }
    fclose(fp);
  } else {
    WARN("failed to open file '%s'\n", filename.c_str());
    success_ = false;
  }

  if (success_) {
    static_assert(sizeof(SignedHeader) == 1024,
                  "SignedHeader should be 1024 bytes");
    if (withSignature) {
      // signed images start on 2K boundary.
      if ((low_ & 2047) != 0) {
        WARN("signed images should start on 2K boundary, not %08x\n", low_);
      }
      base_ = low_;
    } else {
      // unsigned images start on odd 1K boundary.
      if ((low_ & 2047) != 1024) {
        WARN("unsigned images should start odd 1K boundary, not %08x\n", low_);
      }
      base_ = low_ - sizeof(SignedHeader);
    }
  }

  if (success_) {
    VERBOSE("low %08x, high %08x\n", FLASH_START * 16 + low_,
            FLASH_START * 16 + high_);
    // Round image to multiple of 2K.
    high_ = ((high_ + 2047) / 2048) * 2048;
    ro_base_ = FLASH_START * 16 + base_;
    rx_base_ = FLASH_START * 16 + base_;
    ro_max_ = FLASH_START * 16 + base_ + size();
    rx_max_ = FLASH_START * 16 + base_ + size();
    VERBOSE("base %08lx, size %08lx\n", ro_base_, size());
  }

  return success_;
}

Image::~Image() {}

void Image::toIntelHex(FILE* fout) const {
  for (int i = base_; i < high_; i += 16) {
    // spit out segment record at start of segment.
    if (!((i - base_) & 0xffff)) {
      int s = FLASH_START + (base_ >> 4) + ((i - base_) >> 4);
      fprintf(fout, ":02000002%04X%02X\n", s,
              (~((2 + 2 + (s >> 8)) & 255) + 1) & 255);
    }
    // spit out data records, 16 bytes each.
    fprintf(fout, ":10%04X00", (i - base_) & 0xffff);
    int crc = 16 + (((i - base_) >> 8) & 255) + ((i - base_) & 255);
    for (int n = 0; n < 16; ++n) {
      fprintf(fout, "%02X", mem_[i + n]);
      crc += mem_[i + n];
    }
    fprintf(fout, "%02X", (~(crc & 255) + 1) & 255);
    fprintf(fout, "\n");
  }
}

void Image::fillPattern(uint32_t pattern) {
  for (int i = high_ - base_; i < 512 * 1024 - 2048; i += 4) {
    *(uint32_t*)(mem_ + i) = pattern;
  }
  high_ = 512 * 1024 - 2048;
}

void Image::fillRandom() {
  srand(time(NULL));
  for (int i = high_ - base_; i < 512 * 1024 - 2048; i += 4) {
    *(uint32_t*)(mem_ + i) = rand();
  }
  high_ = 512 * 1024 - 2048;
}

void Image::generate(const std::string& filename, bool hex_output) const {
  FILE* fout = fopen(filename.c_str(), "w");
  if (!fout) return;

  if (hex_output)
    toIntelHex(fout);
  else  // TODO: we don't expect write to fail, can be made more robust.
    fwrite(mem_ + base_, 1, high_ - base_, fout);
  fclose(fout);
}

int Image::nibble(char n) {
  switch (n) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      return n - '0';
    case 'a':
    case 'A':
      return 10;
    case 'b':
    case 'B':
      return 11;
    case 'c':
    case 'C':
      return 12;
    case 'd':
    case 'D':
      return 13;
    case 'e':
    case 'E':
      return 14;
    case 'f':
    case 'F':
      return 15;
    default:
      WARN("bad hex digit '%c'\n", n);
      success_ = false;
      return 0;
  }
}

int Image::parseByte(char** p) {
  int result = nibble(**p);
  result *= 16;
  (*p)++;
  result |= nibble(**p);
  (*p)++;
  return result;
}

int Image::parseWord(char** p) {
  int result = parseByte(p);
  result *= 256;
  result |= parseByte(p);
  return result;
}

void Image::store(int adr, int v) {
  if (adr < 0 || (size_t)(adr) >= sizeof(mem_)) {
    WARN("illegal adr %04x\n", adr);
    success_ = false;
    return;
  }
  // VERBOSE("mem_[0x%08x]=0x%02x\n", adr, v&255);
  mem_[adr] = v;
  if (adr > high_) high_ = adr;
  if (adr < low_) low_ = adr;
}

bool Image::sign(PublicKey& key, const SignedHeader* input_hdr,
                 const uint32_t fuses[FUSE_MAX], const uint32_t info[INFO_MAX],
                 const string& hashesFilename) {
  BIGNUM* sig = NULL;
  SignedHeader* hdr = (SignedHeader*)(&mem_[base_]);
  SHA256_CTX sha256;
  int result;

  // List of hashes we actually sign.
  struct {
    uint8_t img_hash[SHA256_DIGEST_LENGTH];
    uint8_t fuses_hash[SHA256_DIGEST_LENGTH];
    uint8_t info_hash[SHA256_DIGEST_LENGTH];
  } hashes;

  memcpy(hdr, input_hdr, sizeof(SignedHeader));

  hdr->image_size = this->size();

  // Fill in key traits
  hdr->keyid = key.n0inv();
  key.modToArray(hdr->key, key.rwords());

  // Hash fuses
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, fuses, FUSE_MAX * sizeof(uint32_t));
  SHA256_Final(hashes.fuses_hash, &sha256);

  hdr->fuses_chk_ = (hashes.fuses_hash[0] << 0) | (hashes.fuses_hash[1] << 8) |
                    (hashes.fuses_hash[2] << 16) | (hashes.fuses_hash[3] << 24);

  // Hash info
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, info, INFO_MAX * sizeof(uint32_t));
  SHA256_Final(hashes.info_hash, &sha256);

  hdr->info_chk_ = (hashes.info_hash[0] << 0) | (hashes.info_hash[1] << 8) |
                   (hashes.info_hash[2] << 16) | (hashes.info_hash[3] << 24);

  // Hash img
  int size = this->size() - offsetof(SignedHeader, tag);
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, &hdr->tag, size);
  SHA256_Final(hashes.img_hash, &sha256);

  hdr->img_chk_ = (hashes.img_hash[0] << 0) | (hashes.img_hash[1] << 8) |
                  (hashes.img_hash[2] << 16) | (hashes.img_hash[3] << 24);

  // Dump out values for comparing against boot_rom
  VERBOSE("Himg =");
  for (size_t i = 0; i < sizeof(hashes.img_hash); ++i) {
    VERBOSE("%02x", hashes.img_hash[i]);
  }
  VERBOSE("\n");

  VERBOSE("Hfss =");
  for (size_t i = 0; i < sizeof(hashes.fuses_hash); ++i) {
    VERBOSE("%02x", hashes.fuses_hash[i]);
  }
  VERBOSE("\n");

  VERBOSE("Hinf =");
  for (size_t i = 0; i < sizeof(hashes.info_hash); ++i) {
    VERBOSE("%02x", hashes.info_hash[i]);
  }
  VERBOSE("\n");

  if (!hashesFilename.empty()) {
    // Write hashes to file for subsequent extraneous (re)signing.
    int fd = open(hashesFilename.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd >= 0) {
      write(fd, &hashes, sizeof(hashes));
      close(fd);
    }
  }

  sig = BN_bin2bn((uint8_t*)(hdr->signature), sizeof(hdr->signature), NULL);

  result = key.sign(&hashes, sizeof(hashes), &sig);

  if (result != 1) {
    WARN("key.sign: %d\n", result);
  } else {
    key.toArray(hdr->signature, key.rwords(), sig);
  }

  if (sig) BN_free(sig);

  return result == 1;
}
