#!/usr/bin/env python3
# -*- coding: utf-8 -*-"

# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script to pack EC binary with manifest header according to
# Based on 607297_Host_ISH_Firmware_Load_Chrome_OS_SAS_Rev0p5.pdf,
# https://chrome-internal.googlesource.com/chromeos/intel-ish/+/refs/heads/upstream/master/modules/api/ish_api/include/loader_common.h#211,
# and b/124788278#comment10

"""Script to pack EC binary with manifest header.

Package ecos main FW binary (kernel) and AON task binary into final EC binary
image with a manifest header, ISH shim loader will parse this header and load
each binaries into right memory location.
"""

import argparse
import struct


MANIFEST_ENTRY_SIZE = 0x80
HEADER_SIZE = 0x1000
PAGE_SIZE = 0x1000


def parseargs():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-k",
        "--kernel",
        help="EC kernel binary to pack, usually ec.RW.bin or ec.RW.flat.",
        required=True,
    )
    parser.add_argument(
        "--kernel-size", type=int, help="Size of EC kernel image", required=True
    )
    parser.add_argument(
        "-a",
        "--aon",
        help="EC aontask binary to pack, usually ish_aontask.bin.",
        required=False,
    )
    parser.add_argument(
        "--aon-size", type=int, help="Size of EC aontask image", required=False
    )
    parser.add_argument("-o", "--output", help="Output flash binary file")

    return parser.parse_args()


def gen_manifest(ext_id, comp_app_name, code_offset, module_size):
    """Returns a binary blob that represents a manifest entry"""
    manifest = bytearray(MANIFEST_ENTRY_SIZE)

    # 4 bytes of ASCII encode ID (little endian)
    struct.pack_into("<4s", manifest, 0, ext_id)
    # 8 bytes of ASCII encode ID (little endian)
    struct.pack_into("<8s", manifest, 32, comp_app_name)
    # 4 bytes of code offset (little endian)
    struct.pack_into("<I", manifest, 96, code_offset)
    # 2 bytes of module in page size increments (little endian)
    struct.pack_into("<H", manifest, 100, module_size)

    return manifest


def roundup_page(size):
    """Returns roundup-ed page size from size of bytes"""
    return int(size / PAGE_SIZE) + (size % PAGE_SIZE > 0)


def main():
    """Main function."""
    args = parseargs()
    print("    Packing EC image file for ISH")

    with open(args.output, "wb") as output_file:
        print("      kernel binary size:", args.kernel_size)
        kern_rdup_pg_size = roundup_page(args.kernel_size)
        # Add manifest for main ISH binary
        output_file.write(
            gen_manifest(b"ISHM", b"ISH_KERN", HEADER_SIZE, kern_rdup_pg_size)
        )

        if args.aon is not None:
            print("      AON binary size:   ", args.aon_size)
            aon_rdup_pg_size = roundup_page(args.aon_size)
            # Add manifest for aontask binary
            output_file.write(
                gen_manifest(
                    b"ISHM",
                    b"AON_TASK",
                    (
                        HEADER_SIZE
                        + kern_rdup_pg_size * PAGE_SIZE
                        - MANIFEST_ENTRY_SIZE
                    ),
                    aon_rdup_pg_size,
                )
            )

        # Add manifest that signals end of manifests
        output_file.write(gen_manifest(b"ISHE", b"", 0, 0))

        # Pad the remaining HEADER with 0s
        if args.aon is not None:
            output_file.write(
                b"\x00" * (HEADER_SIZE - (MANIFEST_ENTRY_SIZE * 3))
            )
        else:
            output_file.write(
                b"\x00" * (HEADER_SIZE - (MANIFEST_ENTRY_SIZE * 2))
            )

        # Append original kernel image
        with open(args.kernel, "rb") as in_file:
            output_file.write(in_file.read())
        # Filling padings due to size round up as pages
        output_file.write(
            b"\x00" * (kern_rdup_pg_size * PAGE_SIZE - args.kernel_size)
        )

        if args.aon is not None:
            # Append original aon image
            with open(args.aon, "rb") as in_file:
                output_file.write(in_file.read())
            # Filling padings due to size round up as pages
            output_file.write(
                b"\x00" * (aon_rdup_pg_size * PAGE_SIZE - args.aon_size)
            )


if __name__ == "__main__":
    main()
