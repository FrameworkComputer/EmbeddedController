/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <common/gnubby.h>

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#include <libusb-1.0/libusb.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include <common/aes.h>
#include <common/ecdh.h>

#include <string>

#define MAX_APDU_SIZE 1200
#define LIBUSB_ERR -1

extern bool FLAGS_verbose;

// Largely from gnubby ifd_driver.c
// -----

typedef int RESPONSECODE;
typedef uint8_t UCHAR;
typedef UCHAR* PUCHAR;
typedef uint32_t DWORD;
typedef DWORD* PDWORD;

#define IFD_SUCCESS 0
#define IFD_COMMUNICATION_ERROR -1

#define DLOG(...) do{if(FLAGS_verbose){fprintf(stderr, __VA_ARGS__);}}while(0)

// usb gnubby commands
#define CMD_ATR  0x81
#define CMD_APDU 0x83
#define CMD_LOCK 0x84
#define CMD_WINK 0x88

// Helper to dump bits to console.
static
void printHex(const char* text, const void* data, int len) {
  int i;
  const uint8_t* d = (const uint8_t*)(data);
  (void)d;
  DLOG("%s: ", text);
  for (i = 0; i < len; ++i) {
    DLOG("%02x", *d++);
    if (i == 3) DLOG(":");
    if (i == 4) DLOG("|");
  }
  DLOG("\n");
}

// Construct usb framed request.
// data can be NULL iff len == 0.
// Returns frame size > 0 on success.
static
RESPONSECODE construct_usb_frame(uint8_t cmd,
                                 const void* data, DWORD len,
                                 uint8_t* out, PDWORD out_len) {
  const uint8_t* d = (const uint8_t*)(data);
  DWORD i;

  if (*out_len < len + 7) return IFD_COMMUNICATION_ERROR;

  // use pid as channel id
  out[0] = getpid() >> 0;
  out[1] = getpid() >> 8;
  out[2] = getpid() >> 16;
  out[3] = getpid() >> 24;
  out[4] = cmd;
  out[5] = len >> 8;
  out[6] = len;

  // Append the actual payload.
  for (i = 0; i < len; ++i) out[7 + i] = d[i];

  // Return total length
  *out_len = 7 + len;

  return IFD_SUCCESS;
}

// Send cmd to gnubby and receive response.
static
RESPONSECODE gnubby_exchange(libusb_device_handle* dev_handle,
                             void* buf, int res,
                             void* rsp, PDWORD rsp_len) {
  int sent_len = 0;
  uint8_t rcv[2048];
  int recv_len = 0;

  DLOG("gnubby_exchange(%p, %p, %d, %p, *%u)\n",
          dev_handle, buf, res, rsp, *rsp_len);

  printHex(">", buf, res);

  // Send to gnubby
  res = libusb_bulk_transfer(dev_handle, (1 | LIBUSB_ENDPOINT_OUT),
                             (unsigned char*)(buf), res,
                             &sent_len, 0);
  DLOG(">: libusb_bulk_transfer: %d [%d]\n", res, sent_len);
  if (res < 0) return IFD_COMMUNICATION_ERROR;

  // Read from gnubby
  memset(rcv, 0, sizeof(rcv));  // start clean.
  res = libusb_bulk_transfer(dev_handle, (1 | LIBUSB_ENDPOINT_IN),
                             rcv, sizeof rcv, &recv_len, 0);
  DLOG("<: libusb_bulk_transfer: %d [%d]\n", res, recv_len);
  if (res < 0) return IFD_COMMUNICATION_ERROR;

  if (recv_len > 0) {
    printHex("<", rcv, recv_len);

    // Check return header.
    // rcv[0..4] should be equal to request.
    // rcv[5..6] is response payload length.
    // rcv[recv_len-2..recv_len-1] is smartcard response code (9000 etc.)
    if (memcmp(buf, rcv, 5)) return IFD_COMMUNICATION_ERROR;

    uint16_t plen = rcv[5] * 256 + rcv[6];
    if (plen + 7 < recv_len) return IFD_COMMUNICATION_ERROR;

    if (*rsp_len < plen) return IFD_COMMUNICATION_ERROR;

    // Copy response payload.
    memcpy(rsp, rcv + 7, plen);

    // Return payload length.
    *rsp_len = plen;

    return IFD_SUCCESS;
  }

  return IFD_COMMUNICATION_ERROR;
}

#if 0
static
RESPONSECODE gnubby_atr(libusb_device_handle* handle, PUCHAR Atr, PDWORD AtrLength) {
  uint8_t cmd[10];
  DWORD cmd_len = sizeof(cmd);
  RESPONSECODE res;

  DLOG("gnubby_atr(%p, %p, *%u)\n", handle, Atr, *AtrLength);

  memset(Atr, 0, *AtrLength);

  res = construct_usb_frame(CMD_ATR, NULL, 0, cmd, &cmd_len);
  if (res != IFD_SUCCESS) return res;

  res = gnubby_exchange(handle, cmd, cmd_len, Atr, AtrLength);

  if (res == IFD_SUCCESS) {
    // Present an ATR that can do T=1
    // Gnubby ATR appears to not advertise that capability.
    memcpy(Atr, "\x3B\xF0\x13\x00\x00\x81\x31\xFE\x45\xE8", 10);
    *AtrLength = 10;
  }

  return res;
}
#endif

static
RESPONSECODE gnubby_apdu(libusb_device_handle* handle,
                         PUCHAR tx, DWORD txLen,
                         PUCHAR rx, PDWORD rxLen) {
  uint8_t cmd[2048];
  DWORD cmd_len = sizeof(cmd);
  RESPONSECODE res = IFD_SUCCESS;

  DLOG("gnubby_apdu(%p, %p, %u, %p, *%u)\n",
       handle, tx, txLen, rx, *rxLen);

  res = construct_usb_frame(CMD_APDU, tx, txLen, cmd, &cmd_len);
  if (res != IFD_SUCCESS) return res;

  res = gnubby_exchange(handle, cmd, cmd_len, rx, rxLen);

  if (res != IFD_SUCCESS) *rxLen = 0;
  return res;
}

static
RESPONSECODE gnubby_lock(libusb_device_handle* handle, UCHAR seconds) {
  uint8_t cmd[10];
  DWORD cmd_len = sizeof(cmd);
  uint8_t rsp[10];
  DWORD rsp_len = sizeof(rsp);

  RESPONSECODE res = IFD_SUCCESS;

  res = construct_usb_frame(CMD_LOCK, &seconds, 1, cmd, &cmd_len);
  if (res != IFD_SUCCESS) return res;

  res = gnubby_exchange(handle, cmd, cmd_len, rsp, &rsp_len);
  if (res != IFD_SUCCESS) return res;

  if ((rsp_len == 1 && rsp[0] == 0) ||
      rsp_len == 0) {
    return IFD_SUCCESS;
  }

  return IFD_COMMUNICATION_ERROR;
}

static
RESPONSECODE gnubby_wink(libusb_device_handle* handle) {
  uint8_t cmd[10];
  DWORD cmd_len = sizeof(cmd);
  uint8_t rsp[10];
  DWORD rsp_len = sizeof(rsp);

  RESPONSECODE res = IFD_SUCCESS;

  res = construct_usb_frame(CMD_WINK, NULL, 0, cmd, &cmd_len);
  if (res != IFD_SUCCESS) return res;

  res = gnubby_exchange(handle, cmd, cmd_len, rsp, &rsp_len);
  if (res != IFD_SUCCESS) return res;

  return res;
}

// -----
// end of ifd_driver cut&paste


// Open a usb device and return (handle_, context).
Gnubby::Gnubby()
    : handle_(NULL) {
  libusb_init(&ctx_);
  libusb_set_debug(ctx_, 3);
}

// Close a usb device.
Gnubby::~Gnubby() {
  // Close handle_ if non-zero.
  if (handle_) {
    int rc = libusb_release_interface(handle_, 0);
    DLOG("gnubby release : %d\n", rc);
    libusb_close(handle_);
    (void) rc;
  }

  // Close context.
  libusb_exit(ctx_);
}


static
int getSW12(const uint8_t* buf, size_t buflen) {
  if (buflen < 2) return -1;
  return buf[buflen - 2] * 256 + buf[buflen - 1];
}

static
void getPIN(uint8_t* out) {
  srand(time(NULL));  // yuk
  for (int i = 0; i < 16; ++i) out[i] = (uint32_t)rand() >> (i+1);

  const char* pin = getpass("Gnubby PIN: ");
  int len = strlen(pin);

  if (len == 6) {
    // Exactly 6, copy direct.
    memcpy(out, pin, 6);
  } else {
    // SHA256, take first 6.
    uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha;
    SHA256_Init(&sha);
    SHA256_Update(&sha, pin, len);
    SHA256_Final(digest, &sha);
    memcpy(out, digest, 6);
  }
}

static
std::string tokenFilename(const uint8_t* fp) {
  const char* home = getenv("HOME");
  if (home == NULL)
    home = getpwuid(getuid())->pw_dir;
  std::string s(home);
  s.append("/.tmp/");
  for (int i = 0; i < 32; ++i) {
    s.push_back("0123456789abcdef"[fp[i]>>4]);
    s.push_back("0123456789abcdef"[fp[i]&15]);
  }
  s.append(".token");
  return s;
}

static
bool getToken(const uint8_t* fp, uint8_t* out) {
  int fd = open(tokenFilename(fp).c_str(), O_RDONLY);
  if (fd < 0) return false;
  int n = read(fd, out, 16);
  close(fd);
  DLOG("read %d from %s\n", n, tokenFilename(fp).c_str());
  return n == 16;
}

static
void saveToken(const uint8_t* fp, const uint8_t* token) {
  int fd = open(tokenFilename(fp).c_str(), O_CREAT|O_TRUNC|O_APPEND|O_NOFOLLOW|O_WRONLY, 0600);
  if (fd >= 0) {
    int n = write(fd, token, 16);
    DLOG("wrote %d to %s\n", n, tokenFilename(fp).c_str());
    close(fd);
    (void) n;
  }
}

static
void forgetToken(const uint8_t* fp) {
  DLOG("forgetting token %s\n", tokenFilename(fp).c_str());
  unlink(tokenFilename(fp).c_str());
}

static
bool isGnubby(libusb_device *dev) {
  struct libusb_device_descriptor lsb_desc;
  if (!libusb_get_device_descriptor(dev, &lsb_desc)) {
    return lsb_desc.idVendor == 0x1050 && lsb_desc.idProduct == 0x0211;
  }
  return false;
}

int Gnubby::doSign(EVP_MD_CTX* ctx, uint8_t* padded_req, uint8_t* signature,
                   uint32_t* siglen, EVP_PKEY* key) {
  RESPONSECODE result = -1;

  uint8_t fp[32];
  ECDH ecdh;
  uint8_t secret[32] = {0};
  AES aes;
  uint8_t pin[16];
  uint8_t token[16];

  UCHAR req[1024];
  UCHAR resp[2048];
  DWORD resp_len = 0;

  // lock(100)
  result = gnubby_lock(handle_, (UCHAR)100);
  if (result != 0) goto __fail;
  // TODO: handle busy etc.

  // select ssh applet
  resp_len = sizeof(resp);
  result = gnubby_apdu(handle_,
                       (PUCHAR)"\x00\xa4\x04\x00\x06\x53\x53\x48\x00\x01\x01", 11,
                       resp, &resp_len);
  if (result != 0) goto __fail;
  if (getSW12(resp, resp_len) != 0x9000) goto __fail;

__again:

  // read slot 0x66 under challenge
  resp_len = sizeof(resp);
  result = gnubby_apdu(handle_,
                       (PUCHAR)"\x00\x43\x66\x00\x00\x00\x10\xff\xee\xdd\xcc\xbb\xaa\x99\x88\x77\x66\x55\x44\x33\x22\x11\x00", 5 + 2 + 16,
                       resp, &resp_len);
  if (result != 0) goto __fail;
  if (getSW12(resp, resp_len) != 0x9000) goto __fail;

  // save device fingerprint
  memcpy(fp, resp + 1 + 256 + 65 + 65, 32);

  uint8_t pubkey[256];
  if (key->type != EVP_PKEY_RSA
      || EVP_PKEY_size(key) != 256
      || BN_bn2bin(key->pkey.rsa->n, pubkey) != 256) {
    goto __fail;
  }
  if (memcmp(pubkey, resp + 1, 256)) {
    // Key mis-match, wrong gnubby selected?
    DLOG("pubkey mis-match, at device handle: %p", handle_);
    goto __fail;
  }

  if (!getToken(fp, token)) {
    // PIN unlock required.
    getPIN(pin);

    ecdh.compute_secret(resp + 1 + 256, secret);
    aes.set_key(secret);

    memcpy(req, "\x00\x42\x00\x00\x51", 5);

    ecdh.get_point(req + 5);
    printHex("req", req, 5 + 65);
    aes.encrypt_block(pin, req + 5 + 65);
    printHex("req", req, 5 + 65 + 16);

    resp_len = sizeof(resp);
    result = gnubby_apdu(handle_,
                         req, 5 + 65 + 16,
                         resp, &resp_len);
    if (result != 0) goto __fail;

    if ((getSW12(resp, resp_len) & 0xfff0) == 0x63c0) {
      // Wrong PIN.
      goto __again;
    }

    if (getSW12(resp, resp_len) != 0x9000) goto __fail;

    aes.set_key(secret + 16);
    aes.decrypt_block(resp, token);

    saveToken(fp, token);
  }

  // Build sign request using slot 0x66.
  memset(req, 0, sizeof(req));
  memcpy(req, "\x00\x40\x66\x00\x00\x01\x10", 7);
  memcpy(req + 7 + 16, padded_req, 256);

  aes.set_key(token);
  aes.cmac(req + 7 + 16, 256, req + 7);

  for (;;) {
    resp_len = sizeof(resp);
    result = gnubby_apdu(handle_,
                         req, 7 + 256 + 16,
                         resp, &resp_len);
    if (result != 0) goto __fail;

    if (getSW12(resp, resp_len) == 0x6985) {  // touch
      gnubby_wink(handle_);
      fprintf(stderr, "touch..");
      fflush(stderr);
      usleep(200000);  // slow down, buddy
      continue;
    }

    if (getSW12(resp, resp_len) == 0x63ca) {  // pin
      forgetToken(fp);
      goto __again;
    }

    break;
  }

  if (getSW12(resp, resp_len) != 0x9000) goto __fail;

  // Return signature.
  memcpy(signature, resp, 256);
  *siglen = 256;

  // profit!
  result = 1;

__fail:
  // (always try to) unlock
  gnubby_lock(handle_, 0);

  return result;
}

int Gnubby::sign(EVP_MD_CTX* ctx, uint8_t* signature, uint32_t* siglen,
                 EVP_PKEY* key) {
  // build pkcs1.5 request for ctx hash
  // lock(100)
  // select ssh
  // read slot 0x66
  // compare against key
  // try read token from ~/.tmp/attest-fp
  // loop
  //  - send sign request
  //  - handle PIN, touch
  //  unlock()
  //  profit!
  RESPONSECODE result = -1;
  DWORD image_hash_len = SHA256_DIGEST_LENGTH;
  libusb_device **device_list;
  ssize_t num_devices = libusb_get_device_list(ctx_, &device_list);

  // Compute pkc15 padded inputs for requested sha256.
  // Brutal hard-coding ftw.
  uint8_t padded_req[256];
  memset(padded_req, 0xff, sizeof(padded_req));
  padded_req[0] = 0x00;
  padded_req[1] = 0x01;
  // Fixed asn1 riddle for sha256
  memcpy(padded_req + 256 - 32 - 20,
         "\x00\x30\x31\x30\x0d\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x01\x05\x00\x04\x20",
         20);
  // Append sha256
  EVP_DigestFinal_ex(ctx,
                     padded_req + sizeof(padded_req) - SHA256_DIGEST_LENGTH,
                     &image_hash_len);

  for (int i = 0; i < num_devices; i++) {
    if (!isGnubby(device_list[i])) {
      continue;
    }
    int rc = libusb_open(device_list[i], &handle_);
    if (rc) {
      DLOG("libusb_open() @ device index: %d failed: %d\n", i, rc);
      continue;
    }
    rc = libusb_claim_interface(handle_, 0);
    if (rc) {
      DLOG("libusb_claim_interface() @ device index: %d failed: %d\n", i, rc);
      if (handle_) {
        libusb_close(handle_);
        handle_ = NULL;
      }
      continue;
    }
    rc = doSign(ctx, padded_req, signature, siglen, key);
    libusb_release_interface(handle_, 0);
    libusb_close(handle_);
    handle_ = NULL;
    if (rc == 1) {
      result = 1;
      break;
    }
    // Try next device.
  }

  libusb_free_device_list(device_list, 1);
  return result;
}

// Open a gnubby, unspecified selection made when multiple plugged in.
int Gnubby::open() {
  RESPONSECODE result = -1;
  handle_ = libusb_open_device_with_vid_pid(
      ctx_,
      0x1050,  // Gnubby Vendor ID (VID)
      0x0211   // Gnubby Product ID (PID)
                                            );
  DLOG("gnubby dev_handle_ %p\n", handle_);
  int rc = libusb_claim_interface(handle_, 0);
  DLOG("gnubby claim : %d\n", rc);

  if (rc != 0) {
    if (handle_) libusb_close(handle_);
  } else {
    result = 1;
  }
  return result;
}

int Gnubby::write_bn(uint8_t p1, BIGNUM* n, size_t length) {
  RESPONSECODE result = -1;

  UCHAR req[1024];
  UCHAR resp[1024];
  DWORD resp_len = 0;

  memcpy(req, "\x00\x66\x00\x00\x00\x00\x00", 7);
  req[2] = p1;
  req[5] = length >> 8;
  req[6] = length;

  if (BN_bn2bin(n, req + 7) != int(length)) goto __fail;

  resp_len = sizeof(resp);
  result = gnubby_apdu(handle_,
                       req, 7 + length,
                       resp, &resp_len);
  if (result != 0) goto __fail;

  result = 1;
  if (getSW12(resp, resp_len) != 0x9000) goto __fail;

  result = 0;

__fail:
  return result;
}

int Gnubby::write(RSA* rsa) {
  RESPONSECODE result = -1;

  UCHAR resp[2048];
  DWORD resp_len = 0;

  if (!handle_) {
    result = (open() != 1);
    if (result) goto __fail;
  }

  // lock(100)
  result = gnubby_lock(handle_, (UCHAR)100);
  if (result != 0) goto __fail;
  // TODO: handle busy etc.

  // select ssh applet
  resp_len = sizeof(resp);
  result = gnubby_apdu(handle_,
                       (PUCHAR)"\x00\xa4\x04\x00\x06\x53\x53\x48\x00\x01\x01", 11,
                       resp, &resp_len);
  if (result != 0) goto __fail;

  result = 1;
  if (getSW12(resp, resp_len) != 0x9000) goto __fail;

  result = 0;

  result = result || write_bn(0, rsa->p, 128);
  result = result || write_bn(1, rsa->q, 128);
  result = result || write_bn(2, rsa->dmp1, 128);
  result = result || write_bn(3, rsa->dmq1, 128);
  result = result || write_bn(4, rsa->iqmp, 128);
  result = result || write_bn(5, rsa->n, 256);
  result = result || write_bn(6, rsa->e, 1);

__fail:
  // (always try to) unlock
  if (handle_) gnubby_lock(handle_, 0);

  return result;
}
