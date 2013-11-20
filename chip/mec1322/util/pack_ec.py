#!/usr/bin/env python

# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script to pack EC binary into SPI flash image for MEC1322

# TODO(crosbug.com/p/24107): Add reference to document on image format.

import hashlib
import os
import struct
import subprocess
import tempfile

# TODO(crosbug.com/p/24188): Make these options available as command line
#                            arguments.
SPI_SIZE = 4 * 1024 * 1024
HEADER_PEM_FILE = 'rsakey_sign_header.pem'
PAYLOAD_PEM_FILE = 'rsakey_sign_payload.pem'
PAYLOAD_FILE = 'ec.bin' # Set to 'ec.RO.flat' for RO-only image
HEADER_LOCATION = 0x170000
HEADER_FLAG1 = 0x00
HEADER_FLAG2 = 0x01
SPI_READ_CMD = 0x01
LOAD_ADDR = 0x100000
SPI_CHIP_SELECT = 0
HEADER_SIZE = 0x140
PAYLOAD_OFFSET = 0x240
OUTPUT_FILE = 'ec.4mb.bin'

CRC_TABLE = [0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
             0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d]

def Crc8(crc, data):
  """Update CRC8 value."""
  data_bytes = map(lambda b: ord(b) if isinstance(b, str) else b, data)
  for v in data_bytes:
    crc = ((crc << 4) & 0xff) ^ (CRC_TABLE[(crc >> 4) ^ (v >> 4)]);
    crc = ((crc << 4) & 0xff) ^ (CRC_TABLE[(crc >> 4) ^ (v & 0xf)]);
  return crc ^ 0x55

def GetEntryPoint(payload_file):
  """Read entry point from payload EC image."""
  with open(payload_file, 'rb') as f:
    f.seek(4)
    s = f.read(4)
  return struct.unpack('<I', s)[0]

def GetPayload(payload_file):
  """Read payload and pad it to 64-byte aligned."""
  with open(payload_file, 'rb') as f:
    payload = bytearray(f.read())
  rem_len = len(payload) % 64
  if rem_len:
    payload += '\0' * (64 - rem_len)
  return payload

def GetPublicKey(pem_file):
  """Extract public exponent and modulus from PEM file."""
  s = subprocess.check_output(['openssl', 'rsa', '-in', pem_file,
                               '-text', '-noout'])
  modulus_raw = []
  in_modulus = False
  for line in s.split('\n'):
    if line.startswith('modulus'):
      in_modulus = True
    elif not line.startswith(' '):
      in_modulus = False
    elif in_modulus:
      modulus_raw.extend(line.strip().strip(':').split(':'))
    if line.startswith('publicExponent'):
      exp = int(line.split(' ')[1], 10)
  modulus_raw.reverse()
  modulus = bytearray(''.join(map(lambda x: chr(int(x, 16)),
                                  modulus_raw[0:256])))
  return struct.pack('<Q', exp), modulus

def PadZeroTo(data, size):
  data.extend('\0' * (size - len(data)))

def BuildHeader(payload_len):
  header = bytearray(['C', 'S', 'M', 'S', '\0'])
  header.extend([HEADER_FLAG1, HEADER_FLAG2, SPI_READ_CMD])
  header.extend(struct.pack('<I', LOAD_ADDR))
  header.extend(struct.pack('<I', GetEntryPoint(PAYLOAD_FILE)))
  header.append((payload_len >> 6) & 0xff)
  header.append((payload_len >> 14) & 0xff)
  PadZeroTo(header, 0x14)
  header.extend(struct.pack('<I', PAYLOAD_OFFSET))

  exp, modulus = GetPublicKey(PAYLOAD_PEM_FILE)
  PadZeroTo(header, 0x20)
  header.extend(exp)
  PadZeroTo(header, 0x30)
  header.extend(modulus)
  PadZeroTo(header, HEADER_SIZE)

  return header

def SignByteArray(data, pem_file):
  hash_file = tempfile.mkstemp()[1]
  sign_file = tempfile.mkstemp()[1]
  with open(hash_file, 'wb') as f:
    hasher = hashlib.sha256()
    hasher.update(data)
    f.write(hasher.digest())
  subprocess.check_call(['openssl', 'rsautl', '-sign', '-inkey', pem_file,
                         '-keyform', 'PEM', '-in', hash_file,
                         '-out', sign_file])
  with open(sign_file, 'rb') as f:
    signed = list(f.read())
    signed.reverse()
    return bytearray(''.join(signed))

def BuildTag(header_loc):
  tag = bytearray([(header_loc >> 8) & 0xff,
                   (header_loc >> 16) & 0xff,
                   (header_loc >> 24) & 0xff])
  if SPI_CHIP_SELECT != 0:
    tag[2] |= 0x80
  tag.append(Crc8(0, tag))
  return tag


def main():
  spi_list = []

  payload = GetPayload(PAYLOAD_FILE)
  payload_len = len(payload)
  payload_signature = SignByteArray(payload, PAYLOAD_PEM_FILE)
  header = BuildHeader(payload_len)
  header_signature = SignByteArray(header, HEADER_PEM_FILE)
  tag = BuildTag(HEADER_LOCATION)

  spi_list.append((HEADER_LOCATION, header))
  spi_list.append((HEADER_LOCATION + HEADER_SIZE, header_signature))
  spi_list.append((HEADER_LOCATION + PAYLOAD_OFFSET, payload))
  spi_list.append((HEADER_LOCATION + PAYLOAD_OFFSET + payload_len,
                   payload_signature))
  spi_list.append((SPI_SIZE - 256, tag))

  spi_list = sorted(spi_list)

  with open(OUTPUT_FILE, 'wb') as f:
    addr = 0
    for s in spi_list:
      assert addr <= s[0]
      if addr < s[0]:
        f.write('\xff' * (s[0] - addr))
        addr = s[0]
      f.write(s[1])
      addr += len(s[1])
    if addr < SPI_SIZE:
      f.write('\xff' * (SPI_SIZE - addr))

if __name__ == '__main__':
  main()
