#!/usr/bin/env python3
# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script to pack EC binary into SPI flash image for MEC17xx
# Based on MEC170x_ROM_Description.pdf DS00002225C (07-28-17).
import argparse
import hashlib
import os
import struct
import subprocess
import tempfile
import zlib  # CRC32


# MEC1701 has 256KB SRAM from 0xE0000 - 0x120000
# SRAM is divided into contiguous CODE & DATA
# CODE at [0xE0000, 0x117FFF] DATA at [0x118000, 0x11FFFF]
# SPI flash size for board is 512KB
# Boot-ROM TAG is located at SPI offset 0 (two 4-byte tags)
#

LFW_SIZE = 0x1000
LOAD_ADDR = 0x0E0000
LOAD_ADDR_RW = 0xE1000
HEADER_SIZE = 0x40
SPI_CLOCK_LIST = [48, 24, 16, 12]
SPI_READ_CMD_LIST = [0x3, 0xB, 0x3B, 0x6B]

CRC_TABLE = [
    0x00,
    0x07,
    0x0E,
    0x09,
    0x1C,
    0x1B,
    0x12,
    0x15,
    0x38,
    0x3F,
    0x36,
    0x31,
    0x24,
    0x23,
    0x2A,
    0x2D,
]


def mock_print(*args, **kwargs):
    pass


debug_print = mock_print


def Crc8(crc, data):
    """Update CRC8 value."""
    for v in data:
        crc = ((crc << 4) & 0xFF) ^ (CRC_TABLE[(crc >> 4) ^ (v >> 4)])
        crc = ((crc << 4) & 0xFF) ^ (CRC_TABLE[(crc >> 4) ^ (v & 0xF)])
    return crc ^ 0x55


def GetEntryPoint(payload_file):
    """Read entry point from payload EC image."""
    with open(payload_file, "rb") as f:
        f.seek(4)
        s = f.read(4)
    return struct.unpack("<I", s)[0]


def GetPayloadFromOffset(payload_file, offset):
    """Read payload and pad it to 64-byte aligned."""
    with open(payload_file, "rb") as f:
        f.seek(offset)
        payload = bytearray(f.read())
    rem_len = len(payload) % 64
    if rem_len:
        payload += b"\0" * (64 - rem_len)
    return payload


def GetPayload(payload_file):
    """Read payload and pad it to 64-byte aligned."""
    return GetPayloadFromOffset(payload_file, 0)


def GetPublicKey(pem_file):
    """Extract public exponent and modulus from PEM file."""
    result = subprocess.run(
        ["openssl", "rsa", "-in", pem_file, "-text", "-noout"],
        stdout=subprocess.PIPE,
        encoding="utf-8",
    )
    modulus_raw = []
    in_modulus = False
    for line in result.stdout.splitlines():
        if line.startswith("modulus"):
            in_modulus = True
        elif not line.startswith(" "):
            in_modulus = False
        elif in_modulus:
            modulus_raw.extend(line.strip().strip(":").split(":"))
        if line.startswith("publicExponent"):
            exp = int(line.split(" ")[1], 10)
    modulus_raw.reverse()
    modulus = bytearray((int(x, 16) for x in modulus_raw[:256]))
    return struct.pack("<Q", exp), modulus


def GetSpiClockParameter(args):
    assert args.spi_clock in SPI_CLOCK_LIST, (
        "Unsupported SPI clock speed %d MHz" % args.spi_clock
    )
    return SPI_CLOCK_LIST.index(args.spi_clock)


def GetSpiReadCmdParameter(args):
    assert args.spi_read_cmd in SPI_READ_CMD_LIST, (
        "Unsupported SPI read command 0x%x" % args.spi_read_cmd
    )
    return SPI_READ_CMD_LIST.index(args.spi_read_cmd)


def PadZeroTo(data, size):
    data.extend(b"\0" * (size - len(data)))


def BuildHeader(args, payload_len, load_addr, rorofile):
    # Identifier and header version
    header = bytearray(b"PHCM\0")

    # byte[5]
    b = GetSpiClockParameter(args)
    b |= 1 << 2
    header.append(b)

    # byte[6]
    b = 0
    header.append(b)

    # byte[7]
    header.append(GetSpiReadCmdParameter(args))

    # bytes 0x08 - 0x0b
    header.extend(struct.pack("<I", load_addr))
    # bytes 0x0c - 0x0f
    header.extend(struct.pack("<I", GetEntryPoint(rorofile)))
    # bytes 0x10 - 0x13
    header.append((payload_len >> 6) & 0xFF)
    header.append((payload_len >> 14) & 0xFF)
    PadZeroTo(header, 0x14)
    # bytes 0x14 - 0x17
    header.extend(struct.pack("<I", args.payload_offset))

    # bytes 0x14 - 0x3F all 0
    PadZeroTo(header, 0x40)

    # header signature is appended by the caller

    return header


def BuildHeader2(args, payload_len, load_addr, payload_entry):
    # Identifier and header version
    header = bytearray(b"PHCM\0")

    # byte[5]
    b = GetSpiClockParameter(args)
    b |= 1 << 2
    header.append(b)

    # byte[6]
    b = 0
    header.append(b)

    # byte[7]
    header.append(GetSpiReadCmdParameter(args))

    # bytes 0x08 - 0x0b
    header.extend(struct.pack("<I", load_addr))
    # bytes 0x0c - 0x0f
    header.extend(struct.pack("<I", payload_entry))
    # bytes 0x10 - 0x13
    header.append((payload_len >> 6) & 0xFF)
    header.append((payload_len >> 14) & 0xFF)
    PadZeroTo(header, 0x14)
    # bytes 0x14 - 0x17
    header.extend(struct.pack("<I", args.payload_offset))

    # bytes 0x14 - 0x3F all 0
    PadZeroTo(header, 0x40)

    # header signature is appended by the caller

    return header


#
# Compute SHA-256 of data and return digest
# as a bytearray
#
def HashByteArray(data):
    hasher = hashlib.sha256()
    hasher.update(data)
    h = hasher.digest()
    bah = bytearray(h)
    return bah


#
# Return 64-byte signature of byte array data.
# Signature is SHA256 of data with 32 0 bytes appended
#
def SignByteArray(data):
    debug_print("Signature is SHA-256 of data")
    sigb = HashByteArray(data)
    sigb.extend(b"\0" * 32)
    return sigb


# MEC1701H supports two 32-bit Tags located at offsets 0x0 and 0x4
# in the SPI flash.
# Tag format:
#   bits[23:0] correspond to bits[31:8] of the Header SPI address
#       Header is always on a 256-byte boundary.
#   bits[31:24] = CRC8-ITU of bits[23:0].
# Notice there is no chip-select field in the Tag both Tag's point
# to the same flash part.
#
def BuildTag(args):
    tag = bytearray(
        [
            (args.header_loc >> 8) & 0xFF,
            (args.header_loc >> 16) & 0xFF,
            (args.header_loc >> 24) & 0xFF,
        ]
    )
    tag.append(Crc8(0, tag))
    return tag


def BuildTagFromHdrAddr(header_loc):
    tag = bytearray(
        [
            (header_loc >> 8) & 0xFF,
            (header_loc >> 16) & 0xFF,
            (header_loc >> 24) & 0xFF,
        ]
    )
    tag.append(Crc8(0, tag))
    return tag


#
# Creates temporary file for read/write
# Reads binary file containing LFW image_size (loader_file)
# Writes LFW image to temporary file
# Reads RO image at beginning of rorw_file up to image_size
#  (assumes RO/RW images have been padded with 0xFF
# Returns temporary file name
#
def PacklfwRoImage(rorw_file, loader_file, image_size):
    """Create a temp file with the
    first image_size bytes from the loader file and append bytes
    from the rorw file.
    return the filename"""
    fo = tempfile.NamedTemporaryFile(delete=False)  # Need to keep file around
    with open(loader_file, "rb") as fin1:  # read 4KB loader file
        pro = fin1.read()
    fo.write(pro)  # write 4KB loader data to temp file
    with open(rorw_file, "rb") as fin:
        ro = fin.read(image_size)

    fo.write(ro)
    fo.close()
    return fo.name


#
# Generate a test EC_RW image of same size
# as original.
# Preserve image_data structure and fill all
# other bytes with 0xA5.
# useful for testing SPI read and EC build
# process hash generation.
#
def gen_test_ecrw(pldrw):
    debug_print("gen_test_ecrw: pldrw type =", type(pldrw))
    debug_print("len pldrw =", len(pldrw), " = ", hex(len(pldrw)))
    cookie1_pos = pldrw.find(b"\x99\x88\x77\xce")
    cookie2_pos = pldrw.find(b"\xdd\xbb\xaa\xce", cookie1_pos + 4)
    t = struct.unpack("<L", pldrw[cookie1_pos + 0x24 : cookie1_pos + 0x28])
    size = t[0]
    debug_print("EC_RW size =", size, " = ", hex(size))

    debug_print("Found cookie1 at ", hex(cookie1_pos))
    debug_print("Found cookie2 at ", hex(cookie2_pos))

    if cookie1_pos > 0 and cookie2_pos > cookie1_pos:
        for i in range(0, cookie1_pos):
            pldrw[i] = 0xA5
        for i in range(cookie2_pos + 4, len(pldrw)):
            pldrw[i] = 0xA5

    with open("ec_RW_test.bin", "wb") as fecrw:
        fecrw.write(pldrw[:size])


def parseargs():
    rpath = os.path.dirname(os.path.relpath(__file__))

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-i",
        "--input",
        help="EC binary to pack, usually ec.bin or ec.RO.flat.",
        metavar="EC_BIN",
        default="ec.bin",
    )
    parser.add_argument(
        "-o",
        "--output",
        help="Output flash binary file",
        metavar="EC_SPI_FLASH",
        default="ec.packed.bin",
    )
    parser.add_argument(
        "--loader_file", help="EC loader binary", default="ecloader.bin"
    )
    parser.add_argument(
        "-s",
        "--spi_size",
        type=int,
        help="Size of the SPI flash in KB",
        default=512,
    )
    parser.add_argument(
        "-l",
        "--header_loc",
        type=int,
        help="Location of header in SPI flash",
        default=0x1000,
    )
    parser.add_argument(
        "-p",
        "--payload_offset",
        type=int,
        help="The offset of payload from the start of header",
        default=0x80,
    )
    parser.add_argument(
        "-r",
        "--rw_loc",
        type=int,
        help="Start offset of EC_RW. Default is -1 meaning 1/2 flash size",
        default=-1,
    )
    parser.add_argument(
        "--spi_clock",
        type=int,
        help="SPI clock speed. 8, 12, 24, or 48 MHz.",
        default=24,
    )
    parser.add_argument(
        "--spi_read_cmd",
        type=int,
        help="SPI read command. 0x3, 0xB, or 0x3B.",
        default=0xB,
    )
    parser.add_argument(
        "--image_size",
        type=int,
        help="Size of a single image. Default 220KB",
        default=(220 * 1024),
    )
    parser.add_argument(
        "--test_spi",
        action="store_true",
        help="Test SPI data integrity by adding CRC32 in last 4-bytes of RO/RW binaries",
        default=False,
    )
    parser.add_argument(
        "--test_ecrw",
        action="store_true",
        help="Use fixed pattern for EC_RW but preserve image_data",
        default=False,
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable verbose output",
        default=False,
    )

    return parser.parse_args()


# Debug helper routine
def dumpsects(spi_list):
    debug_print("spi_list has {0} entries".format(len(spi_list)))
    for s in spi_list:
        debug_print("0x{0:x} 0x{1:x} {2:s}".format(s[0], len(s[1]), s[2]))


def printByteArrayAsHex(ba, title):
    debug_print(title, "= ")
    count = 0
    for b in ba:
        count = count + 1
        debug_print("0x{0:02x}, ".format(b), end="")
        if (count % 8) == 0:
            debug_print("")
    debug_print("\n")


def print_args(args):
    debug_print("parsed arguments:")
    debug_print(".input  = ", args.input)
    debug_print(".output = ", args.output)
    debug_print(".loader_file = ", args.loader_file)
    debug_print(".spi_size (KB) = ", hex(args.spi_size))
    debug_print(".image_size = ", hex(args.image_size))
    debug_print(".header_loc = ", hex(args.header_loc))
    debug_print(".payload_offset = ", hex(args.payload_offset))
    if args.rw_loc < 0:
        debug_print(".rw_loc = ", args.rw_loc)
    else:
        debug_print(".rw_loc = ", hex(args.rw_loc))
    debug_print(".spi_clock = ", args.spi_clock)
    debug_print(".spi_read_cmd = ", args.spi_read_cmd)
    debug_print(".test_spi = ", args.test_spi)
    debug_print(".verbose = ", args.verbose)


#
# Handle quiet mode build from Makefile
# Quiet mode when V is unset or V=0
# Verbose mode when V=1
#
def main():
    global debug_print

    args = parseargs()

    if args.verbose:
        debug_print = print

    debug_print("Begin MEC17xx pack_ec.py script")

    # MEC17xx maximum 192KB each for RO & RW
    # mec1701 chip Makefile sets args.spi_size = 512
    # Tags at offset 0
    #
    print_args(args)

    spi_size = args.spi_size * 1024
    debug_print("SPI Flash image size in bytes =", hex(spi_size))

    # !!! IMPORTANT !!!
    # These values MUST match chip/mec1701/config_flash_layout.h
    # defines.
    # MEC17xx Boot-ROM TAGs are at offset 0 and 4.
    # lfw + EC_RO starts at beginning of second 4KB sector
    # EC_RW starts at offset 0x40000 (256KB)

    spi_list = []

    debug_print("args.input = ", args.input)
    debug_print("args.loader_file = ", args.loader_file)
    debug_print("args.image_size = ", hex(args.image_size))

    rorofile = PacklfwRoImage(args.input, args.loader_file, args.image_size)

    payload = GetPayload(rorofile)
    payload_len = len(payload)
    # debug
    debug_print("EC_LFW + EC_RO length = ", hex(payload_len))

    # SPI image integrity test
    # compute CRC32 of EC_RO except for last 4 bytes
    # skip over 4KB LFW
    # Store CRC32 in last 4 bytes
    if args.test_spi == True:
        crc = zlib.crc32(bytes(payload[LFW_SIZE : (payload_len - 4)]))
        crc_ofs = payload_len - 4
        debug_print("EC_RO CRC32 = 0x{0:08x} @ 0x{1:08x}".format(crc, crc_ofs))
        for i in range(4):
            payload[crc_ofs + i] = crc & 0xFF
            crc = crc >> 8

    # Chromebooks are not using MEC BootROM ECDSA.
    # We implemented the ECDSA disabled case where
    # the 64-byte signature contains a SHA-256 of the binary plus
    # 32 zeros bytes.
    payload_signature = SignByteArray(payload)
    # debug
    printByteArrayAsHex(payload_signature, "LFW + EC_RO payload_signature")

    # MEC17xx Header is 0x80 bytes with an 64 byte signature
    # (32 byte SHA256 + 32 zero bytes)
    header = BuildHeader(args, payload_len, LOAD_ADDR, rorofile)
    # debug
    printByteArrayAsHex(header, "Header LFW + EC_RO")

    # MEC17xx payload ECDSA not used, 64 byte signature is
    # SHA256 + 32 zero bytes
    header_signature = SignByteArray(header)
    # debug
    printByteArrayAsHex(header_signature, "header_signature")

    tag = BuildTag(args)
    # MEC17xx truncate RW length to args.image_size to not overwrite LFW
    # offset may be different due to Header size and other changes
    # MCHP we want to append a SHA-256 to the end of the actual payload
    # to test SPI read routines.
    debug_print("Call to GetPayloadFromOffset")
    debug_print("args.input = ", args.input)
    debug_print("args.image_size = ", hex(args.image_size))

    payload_rw = GetPayloadFromOffset(args.input, args.image_size)
    debug_print("type(payload_rw) is ", type(payload_rw))
    debug_print("len(payload_rw) is ", hex(len(payload_rw)))

    # truncate to args.image_size
    rw_len = args.image_size
    payload_rw = payload_rw[:rw_len]
    payload_rw_len = len(payload_rw)
    debug_print("Truncated size of EC_RW = ", hex(payload_rw_len))

    payload_entry_tuple = struct.unpack_from("<I", payload_rw, 4)
    debug_print("payload_entry_tuple = ", payload_entry_tuple)

    payload_entry = payload_entry_tuple[0]
    debug_print("payload_entry = ", hex(payload_entry))

    # Note: payload_rw is a bytearray therefore is mutable
    if args.test_ecrw:
        gen_test_ecrw(payload_rw)

    # SPI image integrity test
    # compute CRC32 of EC_RW except for last 4 bytes
    # Store CRC32 in last 4 bytes
    if args.test_spi == True:
        crc = zlib.crc32(bytes(payload_rw[: (payload_rw_len - 32)]))
        crc_ofs = payload_rw_len - 4
        debug_print(
            "EC_RW CRC32 = 0x{0:08x} at offset 0x{1:08x}".format(crc, crc_ofs)
        )
        for i in range(4):
            payload_rw[crc_ofs + i] = crc & 0xFF
            crc = crc >> 8

    payload_rw_sig = SignByteArray(payload_rw)
    # debug
    printByteArrayAsHex(payload_rw_sig, "payload_rw_sig")

    os.remove(rorofile)  # clean up the temp file

    # MEC170x Boot-ROM Tags are located at SPI offset 0
    spi_list.append((0, tag, "tag"))

    spi_list.append((args.header_loc, header, "header(lwf + ro)"))
    spi_list.append(
        (
            args.header_loc + HEADER_SIZE,
            header_signature,
            "header(lwf + ro) signature",
        )
    )
    spi_list.append(
        (args.header_loc + args.payload_offset, payload, "payload(lfw + ro)")
    )

    offset = args.header_loc + args.payload_offset + payload_len

    # No SPI Header for EC_RW as its not loaded by BootROM
    spi_list.append((offset, payload_signature, "payload(lfw_ro) signature"))

    # EC_RW location
    rw_offset = int(spi_size // 2)
    if args.rw_loc >= 0:
        rw_offset = args.rw_loc

    debug_print("rw_offset = 0x{0:08x}".format(rw_offset))

    if rw_offset < offset + len(payload_signature):
        print("ERROR: EC_RW overlaps EC_RO")

    spi_list.append((rw_offset, payload_rw, "payload(rw)"))

    # don't add to EC_RW. We don't know if Google will process
    # EC SPI flash binary with other tools during build of
    # coreboot and OS.
    # offset = rw_offset + payload_rw_len
    # spi_list.append((offset, payload_rw_sig, "payload(rw) signature"))

    spi_list = sorted(spi_list)

    dumpsects(spi_list)

    #
    # MEC17xx Boot-ROM locates TAG at SPI offset 0 instead of end of SPI.
    #
    with open(args.output, "wb") as f:
        debug_print("Write spi list to file", args.output)
        addr = 0
        for s in spi_list:
            if addr < s[0]:
                debug_print(
                    "Offset ",
                    hex(addr),
                    " Length",
                    hex(s[0] - addr),
                    "fill with 0xff",
                )
                f.write(b"\xff" * (s[0] - addr))
                addr = s[0]
                debug_print(
                    "Offset ",
                    hex(addr),
                    " Length",
                    hex(len(s[1])),
                    "write data",
                )

            f.write(s[1])
            addr += len(s[1])

        if addr < spi_size:
            debug_print(
                "Offset ",
                hex(addr),
                " Length",
                hex(spi_size - addr),
                "fill with 0xff",
            )
            f.write(b"\xff" * (spi_size - addr))

        f.flush()


if __name__ == "__main__":
    main()
