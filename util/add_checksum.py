#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utility to add RO SHA256 checksum."""

import argparse
from hashlib import sha256
import subprocess
import sys


def get_fmap_area_prop(file, area_name):
    """Gets offset and size of a FMAP area"""

    proc = subprocess.run(
        ["futility", "dump_fmap", "-p", file, area_name],
        check=True,
        capture_output=True,
        text=True,
    )
    # Output is like "EC_RO 4096 520192"
    area_prop = proc.stdout.split()
    assert len(area_prop) == 3, "Incorrect futility output"
    offset = int(area_prop[1])
    size = int(area_prop[2])

    return offset, size


def main():
    """Main function."""

    parser = argparse.ArgumentParser()

    parser.add_argument("--image", help="Input image", required=True)
    parser.add_argument("--output", help="Output image")
    args = parser.parse_args()

    if args.output:
        output_file = args.output
    else:
        output_file = args.image

    ec_ro_offset, ec_ro_size = get_fmap_area_prop(args.image, "EC_RO")
    ro_checksum_offset, ro_checksum_size = get_fmap_area_prop(
        args.image, "RO_CHECKSUM"
    )
    # Make sure checksum is 32-bytes SHA256
    assert ro_checksum_size == 32, "Invalid checksum size"
    # Make sure checksum is placed at the end of RO
    assert ro_checksum_offset == (
        ec_ro_offset + ec_ro_size - ro_checksum_size
    ), "Invalid checksum offset"
    offset = ec_ro_offset
    size = ec_ro_size - ro_checksum_size

    with open(args.image, "rb") as f:
        image = bytearray(f.read())
        checksum = sha256(image[offset : offset + size])
        print("Generated checksum: " + checksum.hexdigest())

    image[ro_checksum_offset : ro_checksum_offset + ro_checksum_size] = (
        checksum.digest()
    )

    with open(output_file, "wb") as f:
        f.write(image)

    print("Output file: " + output_file)

    return 0


if __name__ == "__main__":
    sys.exit(main())
