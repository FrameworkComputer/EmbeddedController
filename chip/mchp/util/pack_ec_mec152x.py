#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script to pack EC binary into SPI flash image for MEC152x
# Based on MEC1521/MEC1523_ROM_Description.pdf
import argparse
import hashlib
import os
import struct
import subprocess
import tempfile
import zlib  # CRC32


# MEC152xH has 256KB SRAM from 0xE0000 - 0x120000
# SRAM is divided into contiguous CODE & DATA
# CODE at [0xE0000, 0x117FFF] DATA at [0x118000, 0x11FFFF]
# SPI flash size for board is 512KB
# Boot-ROM TAG is located at SPI offset 0 (two 4-byte tags)

LFW_SIZE = 0x1000
LOAD_ADDR = 0x0E0000
LOAD_ADDR_RW = 0xE1000
MEC152X_HEADER_SIZE = 0x140
MEC152X_HEADER_VERSION = 0x02
PAYLOAD_PAD_BYTE = b"\xff"
SPI_ERASE_BLOCK_SIZE = 0x1000
SPI_CLOCK_LIST = [48, 24, 16, 12]
SPI_READ_CMD_LIST = [0x3, 0xB, 0x3B, 0x6B]
SPI_DRIVE_STR_DICT = {2: 0, 4: 1, 8: 2, 12: 3}
CHIP_MAX_CODE_SRAM_KB = 224

MEC152X_DICT = {
    "HEADER_SIZE": 0x140,
    "HEADER_VER": 0x02,
    "PAYLOAD_OFFSET": 0x140,
    "PAYLOAD_GRANULARITY": 128,
    "EC_INFO_BLK_SZ": 128,
    "ENCR_KEY_HDR_SZ": 128,
    "COSIG_SZ": 96,
    "TRAILER_SZ": 160,
    "TAILER_PAD_BYTE": b"\xff",
    "PAD_SIZE": 128,
}

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


# Debug helper routine
def dumpsects(spi_list):
    debug_print("spi_list has {0} entries".format(len(spi_list)))
    for s in spi_list:
        debug_print("0x{0:x} 0x{1:x} {2:s}".format(s[0], len(s[1]), s[2]))


def printByteArrayAsHex(ba, title):
    debug_print(title, "= ")
    if ba == None:
        debug_print("None")
        return

    count = 0
    for b in ba:
        count = count + 1
        debug_print("0x{0:02x}, ".format(b), end="")
        if (count % 8) == 0:
            debug_print("")
    debug_print("")


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
    return int.from_bytes(s, byteorder="little")


def GetPayloadFromOffset(payload_file, offset, padsize):
    """Read payload and pad it to padsize."""
    with open(payload_file, "rb") as f:
        f.seek(offset)
        payload = bytearray(f.read())
    rem_len = len(payload) % padsize
    debug_print(
        "GetPayload: padsize={0:0x} len(payload)={1:0x} rem={2:0x}".format(
            padsize, len(payload), rem_len
        )
    )

    if rem_len:
        payload += PAYLOAD_PAD_BYTE * (padsize - rem_len)
        debug_print(
            "GetPayload: Added {0} padding bytes".format(padsize - rem_len)
        )

    return payload


def GetPayload(payload_file, padsize):
    """Read payload and pad it to padsize"""
    return GetPayloadFromOffset(payload_file, 0, padsize)


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


def GetEncodedSpiDriveStrength(args):
    assert args.spi_drive_str in SPI_DRIVE_STR_DICT, (
        "Unsupported SPI drive strength %d mA" % args.spi_drive_str
    )
    return SPI_DRIVE_STR_DICT.get(args.spi_drive_str)


# Return 0=Slow slew rate or 1=Fast slew rate
def GetSpiSlewRate(args):
    if args.spi_slew_fast == True:
        return 1
    return 0


# Return SPI CPOL = 0 or 1
def GetSpiCpol(args):
    if args.spi_cpol == 0:
        return 0
    return 1


# Return SPI CPHA_MOSI
# 0 = SPI Master drives data is stable on inactive to clock edge
# 1 = SPI Master drives data is stable on active to inactive clock edge
def GetSpiCphaMosi(args):
    if args.spi_cpha_mosi == 0:
        return 0
    return 1


# Return SPI CPHA_MISO 0 or 1
# 0 = SPI Master samples data on inactive to active clock edge
# 1 = SPI Master samples data on active to inactive clock edge
def GetSpiCphaMiso(args):
    if args.spi_cpha_miso == 0:
        return 0
    return 1


def PadZeroTo(data, size):
    data.extend(b"\0" * (size - len(data)))


#
# Build SPI image header for MEC152x
# MEC152x image header size = 320(0x140) bytes
#
# Description using Python slice notation [start:start+len]
#
# header[0:4] = 'PHCM'
# header[4] = header version = 0x02(MEC152x)
# header[5] = SPI clock speed, drive strength, sampling mode
#   bits[1:0] = SPI clock speed: 0=48, 1=24, 2=16, 3=12
#   bits[3:2] = SPI controller pins drive strength
#               00b=2mA, 01b=4mA, 10b=8mA, 11b=12mA
#   bit[4] = SPI controller pins slew rate: 0=slow, 1=fast
#   bit[5] = SPI CPOL: 0=SPI clock idle is low, 1=idle is high
#   bit[6] = CHPHA_MOSI
#       1:data change on first inactive to active clock edge
#       0:data change on first active to inactive clock edge
#   bit[7] = CHPHA_MISO:
#       1: Data captured on first inactive to active clock edge
#       0: Data captured on first active to inactive clock edge
# header[6] Boot-ROM loader flags
#   bits[2:0] = VTR0,1,2 rails. 0=3.3V, 1=1.8V. NOTE VTR1=0 always
#   bits[5:3] = 111b
#   bit[6]: For MEC152x controls authentication
#           0=Authentication disabled. Signature is SHA-384 of FW payload
#           1=Authentication enabled. Signature is ECDSA P-384
#   bit[7]: 0=FW pyload not encrypted, 1=FW payload is encrypted
# header[7]: SPI Flash read command
#            0x03 1-1-1 read freq < 33MHz
#            0x0B 1-1-1 + 8 clocks(data tri-stated)
#            0x3B 1-1-2 + 8 clocks(data tri-stated). Data phase is dual I/O
#            0x6B 1-1-4 + 8 clocks(data tri-stated). Data phase is Quad I/O
#            NOTE: Quad requires SPI flash device QE(quad enable) bit
#            to be factory set. Enabling QE disables HOLD# and WP#
#            functionality of the SPI flash device.
# header[0x8:0xC] SRAM Load address little-endian format
# header[0xC:0x10] SRAM FW entry point. Boot-ROM jumps to
#                  this address on successful load. (little-endian)
# header[0x10:0x12] little-endian format: FW binary size in units of
#                   128 bytes(MEC152x)
# header[0x12:0x14] = 0 reserved
# header[0x14:0x18] = Little-ending format: Unsigned offset from start of
#                     header to FW payload.
#                     MEC152x: Offset must be a multiple of 128
#                     Offset must be > header size.
#                     NOTE: If Authentication is enabled size includes
#                     the appended signature.
# MEC152x:
# header[0x18] = Authentication key select. Set to 0 for no Authentication.
# header[0x19:0x50] = 0 reserved.
# header[0x50:0x80] = ECDSA-384 public key x-coord. = 0 Auth. disabled
# header[0x80:0xB0] = ECDSA-384 public key y-coord. = 0 Auth. disabled
# header[0xB0:0xE0] = SHA-384 digest of header[0:0xB0]
# header[0xE0:0x110] = Header ECDSA-384 signature x-coord. = 0 Auth. disabled
# header[0x110:0x140] = Header ECDSA-384 signature y-coor. = 0 Auth. disabled
#
def BuildHeader2(args, chip_dict, payload_len, load_addr, payload_entry):
    header_size = MEC152X_HEADER_SIZE

    # allocate zero filled header
    header = bytearray(b"\x00" * header_size)
    debug_print("len(header) = ", len(header))

    # Identifier and header version
    header[0:4] = b"PHCM"
    header[4] = MEC152X_HEADER_VERSION

    # SPI frequency, drive strength, CPOL/CPHA encoding same for both chips
    spiFreqMHz = GetSpiClockParameter(args)
    header[5] = (int(spiFreqMHz // 48) - 1) & 0x03
    header[5] |= (GetEncodedSpiDriveStrength(args) & 0x03) << 2
    header[5] |= (GetSpiSlewRate(args) & 0x01) << 4
    header[5] |= (GetSpiCpol(args) & 0x01) << 5
    header[5] |= (GetSpiCphaMosi(args) & 0x01) << 6
    header[5] |= (GetSpiCphaMiso(args) & 0x01) << 7

    # b[0]=0 VTR1 must be 3.3V
    # b[1]=0(VTR2 3.3V), 1(VTR2 1.8V)
    # b[2]=0(VTR3 3.3V), 1(VTR3 1.8V)
    # b[5:3]=111b
    # b[6]=0 No ECDSA
    # b[7]=0 No encrypted FW image
    header[6] = 0x7 << 3
    if args.vtr2_V18 == True:
        header[6] |= 0x02
    if args.vtr3_V18 == True:
        header[6] |= 0x04

    # SPI read command set same for both chips
    header[7] = GetSpiReadCmdParameter(args) & 0xFF

    # bytes 0x08 - 0x0b
    header[0x08:0x0C] = load_addr.to_bytes(4, byteorder="little")
    # bytes 0x0c - 0x0f
    header[0x0C:0x10] = payload_entry.to_bytes(4, byteorder="little")
    # bytes 0x10 - 0x11 payload length in units of 128 bytes

    payload_units = int(payload_len // chip_dict["PAYLOAD_GRANULARITY"])
    assert payload_units < 0x10000, print(
        "Payload too large: len={0} units={1}".format(
            payload_len, payload_units
        )
    )

    header[0x10:0x12] = payload_units.to_bytes(2, "little")

    # bytes 0x14 - 0x17
    header[0x14:0x18] = chip_dict["PAYLOAD_OFFSET"].to_bytes(4, "little")

    # MEC152x: Disable ECDSA and encryption
    header[0x18] = 0

    # header[0xB0:0xE0] = SHA384(header[0:0xB0])
    header[0xB0:0xE0] = hashlib.sha384(header[0:0xB0]).digest()
    # When ECDSA authentication is disabled MCHP SPI image generator
    # is filling the last 48 bytes of the Header with 0xff
    header[-48:] = b"\xff" * 48

    debug_print("After hash: len(header) = ", len(header))

    return header


#
# MEC152x 128-byte EC Info Block appended to
# end of padded FW binary
# bytes 0 through 103 are undefined, we set to 0xFF
# bytes 104 through 119 are rollback permissions
# bytes 120 through 123 are key revocation permissions
# byte 124 = customer platform ID[7:0]
# byte 125 = customer platform ID[15:8]
# byte 126 = customer auto rollback flags
# byte 127 = customer current image revision
#
def GenEcInfoBlock(args, chip_dict):
    ecinfo = bytearray(chip_dict["EC_INFO_BLK_SZ"])
    return ecinfo


#
# Generate SPI FW image co-signature.
# MEC152X cosignature is 96 bytes used by OEM FW
# developer to sign their binary with ECDSA-P384-SHA384 or
# some other signature algorithm that fits in 96 bytes.
# At this time Cros-EC is not using this field, fill with 0xFF.
# If this feature is implemented we need to read the OEM's
# generated signature from a file and extract the binary
# signature.
#
def GenCoSignature(args, chip_dict, payload):
    return bytearray(b"\xff" * chip_dict["COSIG_SZ"])


#
# Generate SPI FW Image trailer.
# MEC152X: Size = 160 bytes
#   binary = payload || encryption_key_header || ec_info_block || cosignature
#   trailer[0:48] = SHA384(binary)
#   trailer[48:144] = 0xFF
#   trailer[144:160] = 0xFF. Boot-ROM spec. says these bytes should be random.
#       Authentication & encryption are not used therefore random data
#       is not necessary.
def GenTrailer(
    args, chip_dict, payload, encryption_key_header, ec_info_block, cosignature
):
    trailer = bytearray(chip_dict["TAILER_PAD_BYTE"] * chip_dict["TRAILER_SZ"])
    hasher = hashlib.sha384()
    hasher.update(payload)
    if ec_info_block != None:
        hasher.update(ec_info_block)
    if encryption_key_header != None:
        hasher.update(encryption_key_header)
    if cosignature != None:
        hasher.update(cosignature)
    trailer[0:48] = hasher.digest()
    trailer[-16:] = 16 * b"\xff"

    return trailer


# MEC152xH supports two 32-bit Tags located at offsets 0x0 and 0x4
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


# FlashMap is an option for MEC152x
# It is a 32 bit structure
# bits[18:0] = bits[30:12] of second SPI flash base address
# bits[23:19] = 0 reserved
# bits[31:24] = CRC8 of bits[23:0]
# Input:
#   integer containing base address of second SPI flash
#   This value is usually equal to the size of the first
#   SPI flash and should be a multiple of 4KB
# Output:
#   bytearray of length 4
def BuildFlashMap(secondSpiFlashBaseAddr):
    flashmap = bytearray(4)
    flashmap[0] = (secondSpiFlashBaseAddr >> 12) & 0xFF
    flashmap[1] = (secondSpiFlashBaseAddr >> 20) & 0xFF
    flashmap[2] = (secondSpiFlashBaseAddr >> 28) & 0xFF
    flashmap[3] = Crc8(0, flashmap)
    return flashmap


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
    parser.add_argument(
        "--tag0_loc", type=int, help="MEC152X TAG0 SPI offset", default=0
    )
    parser.add_argument(
        "--tag1_loc", type=int, help="MEC152X TAG1 SPI offset", default=4
    )
    parser.add_argument(
        "--spi_drive_str",
        type=int,
        help="Chip SPI drive strength in mA: 2, 4, 8, or 12",
        default=4,
    )
    parser.add_argument(
        "--spi_slew_fast",
        action="store_true",
        help="SPI use fast slew rate. Default is False",
        default=False,
    )
    parser.add_argument(
        "--spi_cpol",
        type=int,
        help="SPI clock polarity when idle. Defealt is 0(low)",
        default=0,
    )
    parser.add_argument(
        "--spi_cpha_mosi",
        type=int,
        help="""SPI clock phase master drives data.
                              0=Data driven on active to inactive clock edge,
                              1=Data driven on inactive to active clock edge""",
        default=0,
    )
    parser.add_argument(
        "--spi_cpha_miso",
        type=int,
        help="""SPI clock phase master samples data.
                              0=Data sampled on inactive to active clock edge,
                              1=Data sampled on active to inactive clock edge""",
        default=0,
    )

    parser.add_argument(
        "--vtr2_V18",
        action="store_true",
        help="Chip VTR2 rail is 1.8V. Default is False(3.3V)",
        default=False,
    )

    parser.add_argument(
        "--vtr3_V18",
        action="store_true",
        help="Chip VTR3 rail is 1.8V. Default is False(3.3V)",
        default=False,
    )

    return parser.parse_args()


def print_args(args):
    debug_print("parsed arguments:")
    debug_print(".input  = ", args.input)
    debug_print(".output = ", args.output)
    debug_print(".loader_file = ", args.loader_file)
    debug_print(".spi_size (KB) = ", hex(args.spi_size))
    debug_print(".image_size = ", hex(args.image_size))
    debug_print(".tag0_loc = ", hex(args.tag0_loc))
    debug_print(".tag1_loc = ", hex(args.tag1_loc))
    debug_print(".header_loc = ", hex(args.header_loc))
    if args.rw_loc < 0:
        debug_print(".rw_loc = ", args.rw_loc)
    else:
        debug_print(".rw_loc = ", hex(args.rw_loc))
    debug_print(".spi_clock (MHz) = ", args.spi_clock)
    debug_print(".spi_read_cmd = ", hex(args.spi_read_cmd))
    debug_print(".test_spi = ", args.test_spi)
    debug_print(".test_ecrw = ", args.test_ecrw)
    debug_print(".verbose = ", args.verbose)
    debug_print(".spi_drive_str = ", args.spi_drive_str)
    debug_print(".spi_slew_fast = ", args.spi_slew_fast)
    debug_print(".spi_cpol = ", args.spi_cpol)
    debug_print(".spi_cpha_mosi = ", args.spi_cpha_mosi)
    debug_print(".spi_cpha_miso = ", args.spi_cpha_miso)
    debug_print(".vtr2_V18 = ", args.vtr2_V18)
    debug_print(".vtr3_V18 = ", args.vtr3_V18)


#
# Handle quiet mode build from Makefile
# Quiet mode when V is unset or V=0
# Verbose mode when V=1
#
# MEC152x SPI Image Generator
# No authentication
# No payload encryption
#
# SPI Offset 0x0 = TAG0 points to Header for EC-RO FW
# SPI Offset 0x4 = TAG1 points to Header for EC-RO FW
# TAG Size = 4 bytes
#  bits[23:0] = bits[31:8] of Header SPI offset
#  bits[31:24] = CRC8-ITU checksum of bits[23:0].
#
# MEC152X SPI header and payload layout for minimum size
# header offset aligned on 256 byte boundary
# header_spi_address:
#   header[0:0x4F] = Header data
#   header[0x50:0x80] = ECDSA-P384 public key x for Header authentication
#   header[0x80:0xB0] = ECDSA-P384 public key y for Header authentication
#   header[0xB0:0xE0] = SHA384 digest of header[0:0xB0]
#   header[0xE0:0x110] = ECDSA-P384-SHA384 Signature.R of header[0:0xB0]
#   header[0x110:0x140] = ECDSA-P384-SHA384 Signature.S of header[0:0xB0]
# payload_spi_address = header_spi_address + len(Header)
#       Payload had been padded such that len(padded_payload) % 128 == 0
#   padded_payload[padded_payload_len]
# payload_signature_address = payload_spi_address + len(padded_payload)
#  payload_encryption_key_header[128] Not present if encryption disabled
#  payload_cosignature[96] = 0 if Authentication is disabled
#  payload_trailer[160] = SHA384(padded_payload ||
#                                optional payload_encryption_key_header)
#                       || 48 * [0]
#                       || 48 * [0]
#
def main():
    global debug_print

    args = parseargs()

    if args.verbose:
        debug_print = print

    debug_print("Begin pack_ec_mec152x.py script")

    print_args(args)

    chip_dict = MEC152X_DICT

    # Boot-ROM requires header location aligned >= 256 bytes.
    # CrOS EC flash image update code requires EC_RO/RW location to be aligned
    # on a flash erase size boundary and EC_RO/RW size to be a multiple of
    # the smallest flash erase block size.
    #
    assert (args.header_loc % SPI_ERASE_BLOCK_SIZE) == 0, (
        "Header location %d is not on a flash erase block boundary boundary"
        % args.header_loc
    )

    max_image_size = CHIP_MAX_CODE_SRAM_KB - LFW_SIZE
    if args.test_spi:
        max_image_size -= 32  # SHA256 digest

    assert args.image_size > max_image_size, (
        "Image size exceeds maximum" % args.image_size
    )

    spi_size = args.spi_size * 1024
    debug_print("SPI Flash image size in bytes =", hex(spi_size))

    # !!! IMPORTANT !!!
    # These values MUST match chip/mchp/config_flash_layout.h
    # defines.
    # MEC152x Boot-ROM TAGs are at offset 0 and 4.
    # lfw + EC_RO starts at beginning of second 4KB sector
    # EC_RW starts at (flash size / 2) i.e. 0x40000 for a 512KB flash.

    spi_list = []

    debug_print("args.input = ", args.input)
    debug_print("args.loader_file = ", args.loader_file)
    debug_print("args.image_size = ", hex(args.image_size))

    rorofile = PacklfwRoImage(args.input, args.loader_file, args.image_size)
    debug_print("Temporary file containing LFW + EC_RO is ", rorofile)

    lfw_ecro = GetPayload(rorofile, chip_dict["PAD_SIZE"])
    lfw_ecro_len = len(lfw_ecro)
    debug_print("Padded LFW + EC_RO length = ", hex(lfw_ecro_len))

    # SPI test mode compute CRC32 of EC_RO and store in last 4 bytes
    if args.test_spi:
        crc32_ecro = zlib.crc32(bytes(lfw_ecro[LFW_SIZE:-4]))
        crc32_ecro_bytes = crc32_ecro.to_bytes(4, byteorder="little")
        lfw_ecro[-4:] = crc32_ecro_bytes
        debug_print("ecro len = ", hex(len(lfw_ecro) - LFW_SIZE))
        debug_print("CRC32(ecro-4) = ", hex(crc32_ecro))

    # Reads entry point from offset 4 of file.
    # This assumes binary has Cortex-M4 vector table at offset 0.
    # 32-bit word at offset 0x0 initial stack pointer value
    # 32-bit word at offset 0x4 address of reset handler
    # NOTE: reset address will have bit[0]=1 to ensure thumb mode.
    lfw_ecro_entry = GetEntryPoint(rorofile)

    # Chromebooks are not using MEC BootROM SPI header/payload authentication
    # or payload encryption. In this case the header authentication signature
    # is filled with the hash digest of the respective entity.
    # BuildHeader2 computes the hash digest and stores it in the correct
    # header location.
    header = BuildHeader2(
        args, chip_dict, lfw_ecro_len, LOAD_ADDR, lfw_ecro_entry
    )
    printByteArrayAsHex(header, "Header(lfw_ecro)")

    ec_info_block = GenEcInfoBlock(args, chip_dict)
    printByteArrayAsHex(ec_info_block, "EC Info Block")

    cosignature = GenCoSignature(args, chip_dict, lfw_ecro)
    printByteArrayAsHex(cosignature, "LFW + EC_RO cosignature")

    trailer = GenTrailer(
        args, chip_dict, lfw_ecro, None, ec_info_block, cosignature
    )

    printByteArrayAsHex(trailer, "LFW + EC_RO trailer")

    # Build TAG0. Set TAG1=TAG0 Boot-ROM is allowed to load EC-RO only.
    tag0 = BuildTag(args)
    tag1 = tag0

    debug_print("Call to GetPayloadFromOffset")
    debug_print("args.input = ", args.input)
    debug_print("args.image_size = ", hex(args.image_size))

    ecrw = GetPayloadFromOffset(
        args.input, args.image_size, chip_dict["PAD_SIZE"]
    )
    debug_print("type(ecrw) is ", type(ecrw))
    debug_print("len(ecrw) is ", hex(len(ecrw)))

    # truncate to args.image_size
    ecrw_len = len(ecrw)
    if ecrw_len > args.image_size:
        debug_print(
            "Truncate EC_RW len={0:0x} to image_size={1:0x}".format(
                ecrw_len, args.image_size
            )
        )
        ecrw = ecrw[: args.image_size]
        ecrw_len = len(ecrw)

    debug_print("len(EC_RW) = ", hex(ecrw_len))

    # SPI test mode compute CRC32 of EC_RW and store in last 4 bytes
    if args.test_spi:
        crc32_ecrw = zlib.crc32(bytes(ecrw[0:-4]))
        crc32_ecrw_bytes = crc32_ecrw.to_bytes(4, byteorder="little")
        ecrw[-4:] = crc32_ecrw_bytes
        debug_print("ecrw len = ", hex(len(ecrw)))
        debug_print("CRC32(ecrw) = ", hex(crc32_ecrw))

    # Assume FW layout is standard Cortex-M style with vector
    # table at start of binary.
    # 32-bit word at offset 0x0 = Initial stack pointer
    # 32-bit word at offset 0x4 = Address of reset handler
    ecrw_entry_tuple = struct.unpack_from("<I", ecrw, 4)
    debug_print("ecrw_entry_tuple[0] = ", hex(ecrw_entry_tuple[0]))

    ecrw_entry = ecrw_entry_tuple[0]
    debug_print("ecrw_entry = ", hex(ecrw_entry))

    # Note: payload_rw is a bytearray therefore is mutable
    if args.test_ecrw:
        gen_test_ecrw(ecrw)

    os.remove(rorofile)  # clean up the temp file

    # MEC152X Add TAG's
    spi_list.append((args.tag0_loc, tag0, "tag0"))
    spi_list.append((args.tag1_loc, tag1, "tag1"))

    # flashmap is non-zero only for systems with two external
    # SPI flash chips.
    flashmap = BuildFlashMap(0)
    spi_list.append((8, flashmap, "flashmap"))

    # Boot-ROM SPI image header for LFW+EC-RO
    spi_list.append((args.header_loc, header, "header(lfw + ro)"))
    spi_list.append(
        (args.header_loc + chip_dict["PAYLOAD_OFFSET"], lfw_ecro, "lfw_ecro")
    )

    offset = args.header_loc + chip_dict["PAYLOAD_OFFSET"] + lfw_ecro_len

    if ec_info_block != None:
        spi_list.append((offset, ec_info_block, "EC Info Block"))
        offset += len(ec_info_block)

    if cosignature != None:
        spi_list.append((offset, cosignature, "ECRO Cosignature"))
        offset += len(cosignature)

    if trailer != None:
        spi_list.append((offset, trailer, "ECRO Trailer"))
        offset += len(trailer)

    # EC_RW location
    rw_offset = int(spi_size // 2)
    if args.rw_loc >= 0:
        rw_offset = args.rw_loc

    debug_print("rw_offset = 0x{0:08x}".format(rw_offset))

    assert rw_offset >= offset, print(
        """Offset of EC_RW at {0:08x} overlaps end
               of EC_RO at {1:08x}""".format(
            rw_offset, offset
        )
    )

    spi_list.append((rw_offset, ecrw, "ecrw"))
    offset = rw_offset + len(ecrw)

    spi_list = sorted(spi_list)

    dumpsects(spi_list)

    #
    # MEC152X Boot-ROM locates TAG0/1 at SPI offset 0
    # instead of end of SPI.
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
