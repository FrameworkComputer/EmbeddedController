#!/usr/bin/env python3
# Copyright 2018 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unpacks ftb apparently."""

import argparse
import ctypes
import os
import pathlib


class Header(ctypes.Structure):
    """A container for FTP Header."""

    _pack_ = 1
    _fields_ = [
        ("signature", ctypes.c_uint32),
        ("ftb_ver", ctypes.c_uint32),
        ("chip_id", ctypes.c_uint32),
        ("svn_ver", ctypes.c_uint32),
        ("fw_ver", ctypes.c_uint32),
        ("config_id", ctypes.c_uint32),
        ("config_ver", ctypes.c_uint32),
        ("reserved", ctypes.c_uint8 * 8),
        ("release_info", ctypes.c_ulonglong),
        ("sec_size", ctypes.c_uint32 * 4),
        ("crc", ctypes.c_uint32),
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
    None,  # This section shouldn't exist
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
    """Main function."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", "-i", required=True)
    parser.add_argument("--output", "-o", required=True)
    args = parser.parse_args()

    header_bytes = pathlib.Path(args.input).read_bytes()

    size = len(header_bytes)
    if size < FW_HEADER_SIZE + FW_BYTES_ALIGN:
        raise ValueError("FW size too small")

    print("FTB file size:", size)

    header = Header()
    assert ctypes.sizeof(header) == FW_HEADER_SIZE

    ctypes.memmove(
        ctypes.addressof(header), header_bytes, ctypes.sizeof(header)
    )
    if (
        header.signature != FW_HEADER_SIGNATURE
        or header.ftb_ver != FW_FTB_VER
        or header.chip_id != FW_CHIP_ID
    ):
        raise ValueError("Invalid header")

    for key, _ in header._fields_:  # pylint:disable=protected-access
        value = getattr(header, key)
        if isinstance(value, ctypes.Array):
            print(key, list(map(hex, value)))
        else:
            print(key, hex(value))

    dimension = sum(header.sec_size)

    assert dimension + FW_HEADER_SIZE + FW_BYTES_ALIGN == size
    data = header_bytes[FW_HEADER_SIZE : FW_HEADER_SIZE + dimension]

    with open(args.output, "wb") as output_file:
        # ensure the file size
        output_file.seek(OUTPUT_FILE_SIZE - 1, os.SEEK_SET)
        output_file.write(b"\x00")

        output_file.seek(0, os.SEEK_SET)
        output_file.write(header_bytes[0 : ctypes.sizeof(header)])

        offset = 0
        # write each sections
        for i, addr in enumerate(FLASH_SEC_ADDR):
            size = header.sec_size[i]
            assert addr is not None or size == 0

            if size == 0:
                continue

            output_file.seek(UPDATE_PDU_SIZE + addr, os.SEEK_SET)
            output_file.write(data[offset : offset + size])
            offset += size

        output_file.flush()


if __name__ == "__main__":
    main()
