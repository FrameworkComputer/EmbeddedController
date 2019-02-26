#!/usr/bin/env python

# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script to pack EC binary with manifest header according to
# Based on 607297_Host_ISH_Firmware_Load_Chrome_OS_SAS_Rev0p5.pdf,
# https://chrome-internal.googlesource.com/chromeos/intel-ish/+/refs/heads/upstream/master/modules/api/ish_api/include/loader_common.h#211,
# and b/124788278#comment10

import argparse
import struct

MANIFEST_ENTRY_SIZE = 0x80
HEADER_SIZE = 0x1000
PAGE_SIZE = 0x1000

def parseargs():
  parser = argparse.ArgumentParser()
  parser.add_argument("-i", "--input",
                      help="EC binary to pack, usually ec.bin or ec.RO.flat.")
  parser.add_argument("-o", "--output",
                      help="Output flash binary file")
  parser.add_argument("--image_size", type=int,
                      help="Size of a single image")

  return parser.parse_args()

def gen_manifest(ext_id, comp_app_name, code_offset, module_size):
  """Returns a binary blob that represents a manifest entry"""
  m = bytearray(MANIFEST_ENTRY_SIZE)

  # 4 bytes of ASCII encode ID (little endian)
  struct.pack_into('<4s', m, 0, ext_id)
  # 8 bytes of ASCII encode ID (little endian)
  struct.pack_into('<8s', m, 32, comp_app_name)
  # 4 bytes of code offset (little endian)
  struct.pack_into('<I', m, 96, code_offset)
  # 2 bytes of module in page size increments (little endian)
  struct.pack_into('<H', m, 100, module_size / PAGE_SIZE)

  return m

def main():
  args = parseargs()

  with open(args.output, 'wb') as f:
    # Add manifest for main ISH binary
    f.write(gen_manifest('ISHM', 'ISH_KERN', HEADER_SIZE, args.image_size))
    # Add manifest that signals end of manifests
    f.write(gen_manifest('ISHE', '', 0, 0))
    # Pad the remaining HEADER with 0s
    f.write('\x00' * (HEADER_SIZE - (MANIFEST_ENTRY_SIZE * 2)))

    # Append original image
    with open(args.input, 'rb') as in_file:
      f.write(in_file.read())

if __name__ == '__main__':
  main()