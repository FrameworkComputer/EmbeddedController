#!/usr/bin/env python
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Note: This is a py2/3 compatible file.

from __future__ import print_function
import argparse
import ctypes
import os


class Header(ctypes.Structure):
  _pack_ = 1
  _fields_ = [
      ('signature', ctypes.c_uint32),
      ('ftb_ver', ctypes.c_uint32),
      ('chip_id', ctypes.c_uint32),
      ('svn_ver', ctypes.c_uint32),
      ('fw_ver', ctypes.c_uint32),
      ('config_id', ctypes.c_uint32),
      ('config_ver', ctypes.c_uint32),
      ('reserved', ctypes.c_uint8 * 8),
      ('release_info', ctypes.c_ulonglong),
      ('sec_size', ctypes.c_uint32 * 4),
      ('crc', ctypes.c_uint32),
  ]

FW_HEADER_SIZE = 64
FW_HEADER_SIGNATURE = 0xAA55AA55
FW_FTB_VER = 0x00000001
FW_CHIP_ID = 0x3936
FW_BYTES_ALIGN = 4
FW_BIN_VER_OFFSET = 16
FW_BIN_CONFIG_ID_OFFSET = 20

# Starting address in flash for each section
FLASH_SEC_ADDR = [
    0x0000 * 4,  # CODE
    0x7C00 * 4,  # CONFIG
    0x7000 * 4,  # CX
    None  # This section shouldn't exist
]

UPDATE_PDU_SIZE = 4096

# Bin file format:
#   FTB header (padded to `UPDATE_PDU_SIZE`)
#   Flash sections
#     CODE
#     CX
#     CONFIG
OUTPUT_FILE_SIZE = UPDATE_PDU_SIZE + 128 * 1024


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--input', '-i', required=True)
  parser.add_argument('--output', '-o', required=True)
  args = parser.parse_args()

  with open(args.input, 'rb') as f:
    bs = f.read()

  size = len(bs)
  if size < FW_HEADER_SIZE + FW_BYTES_ALIGN:
    raise Exception('FW size too small')

  print('FTB file size:', size)

  header = Header()
  assert ctypes.sizeof(header) == FW_HEADER_SIZE

  ctypes.memmove(ctypes.addressof(header), bs, ctypes.sizeof(header))
  if (header.signature != FW_HEADER_SIGNATURE or
      header.ftb_ver != FW_FTB_VER or
      header.chip_id != FW_CHIP_ID):
    raise Exception('Invalid header')

  for key, _ in header._fields_:
    v = getattr(header, key)
    if isinstance(v, ctypes.Array):
      print(key, list(map(hex, v)))
    else:
      print(key, hex(v))

  dimension = sum(header.sec_size)

  assert dimension + FW_HEADER_SIZE + FW_BYTES_ALIGN == size
  data = bs[FW_HEADER_SIZE:FW_HEADER_SIZE + dimension]

  with open(args.output, 'wb') as f:
    # ensure the file size
    f.seek(OUTPUT_FILE_SIZE - 1, os.SEEK_SET)
    f.write(b'\x00')

    f.seek(0, os.SEEK_SET)
    f.write(bs[0 : ctypes.sizeof(header)])

    offset = 0
    # write each sections
    for i, addr in enumerate(FLASH_SEC_ADDR):
      size = header.sec_size[i]
      assert addr is not None or size == 0

      if size == 0:
        continue

      f.seek(UPDATE_PDU_SIZE + addr, os.SEEK_SET)
      f.write(data[offset : offset + size])
      offset += size

    f.flush()


if __name__ == '__main__':
  main()
