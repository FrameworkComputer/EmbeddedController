#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint:disable=too-many-lines

"""A script to pack EC binary into SPI flash image for MEC172x
Based on MEC172x_ROM_Description.pdf revision 6/8/2020
"""

import argparse
import hashlib
import os
import struct
import subprocess
import tempfile
import zlib  # CRC32


# MEC172x has 416KB SRAM from 0xC0000 - 0x127FFF
# SRAM is divided into contiguous CODE & DATA
# CODE at [0xC0000, 0x117FFF] DATA at [0x118000, 0x127FFF]
# Google EC SPI flash size for board is currently 512KB
# split into 1/2.
# EC_RO: 0 - 0x3FFFF
# EC_RW: 0x40000 - 0x7FFFF
#
SPI_ERASE_BLOCK_SIZE = 0x1000
SPI_CLOCK_LIST = [48, 24, 16, 12, 96]
SPI_READ_CMD_LIST = [0x3, 0xB, 0x3B, 0x6B]
SPI_DRIVE_STR_DICT = {2: 0, 4: 1, 8: 2, 12: 3}
# Maximum EC_RO/EC_RW code size is based upon SPI flash erase
# sector size, MEC172x Boot-ROM TAG, Header, Footer.
# SPI Offset      Description
# 0x00 - 0x07     TAG0 and TAG1
# 0x1000 - 0x113F Boot-ROM SPI Header
# 0x1140 - 0x213F 4KB LFW
# 0x2040 - 0x3EFFF
# 0x3F000 - 0x3FFFF BootROM EC_INFO_BLK || COSIG || ENCR_KEY_HDR(optional) || TRAILER
CHIP_MAX_CODE_SRAM_KB = 256 - 12

MEC172X_DICT = {
    "LFW_SIZE": 0x1000,
    "LOAD_ADDR": 0xC0000,
    "TAG_SIZE": 4,
    "KEY_BLOB_SIZE": 1584,
    "HEADER_SIZE": 0x140,
    "HEADER_VER": 0x03,
    "PAYLOAD_GRANULARITY": 128,
    "PAYLOAD_PAD_BYTE": b"\xff",
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


def mock_print(*_args, **_kwargs):
    """Does nothing"""


debug_print = mock_print


def dumpsects(spi_list):
    """Debug helper routine"""
    debug_print(f"spi_list has {len(spi_list)} entries")
    for sss in spi_list:
        debug_print(f"0x{sss[0]:x} 0x{len(sss[1]):x} {sss[2]:s}")


def printByteArrayAsHex(ba, title):  # pylint:disable=invalid-name
    """Prints a byte array as hex"""
    debug_print(title, "= ")
    if ba is None:
        debug_print("None")
        return

    count = 0
    for bbb in ba:
        count = count + 1
        debug_print(f"0x{bbb:02x}, ", end="")
        if (count % 8) == 0:
            debug_print("")
    debug_print("")


def Crc8(crc, data):  # pylint:disable=invalid-name
    """Update CRC8 value."""
    for vvv in data:
        crc = ((crc << 4) & 0xFF) ^ (CRC_TABLE[(crc >> 4) ^ (vvv >> 4)])
        crc = ((crc << 4) & 0xFF) ^ (CRC_TABLE[(crc >> 4) ^ (vvv & 0xF)])
    return crc ^ 0x55


def GetEntryPoint(payload_file):  # pylint:disable=invalid-name
    """Read entry point from payload EC image."""
    with open(payload_file, "rb") as fff:
        fff.seek(4)
        sss = fff.read(4)
    return int.from_bytes(sss, byteorder="little")


def GetPayloadFromOffset(
    payload_file, offset, chip_dict
):  # pylint:disable=invalid-name
    """Read payload and pad it to chip_dict["PAD_SIZE"]."""
    padsize = chip_dict["PAD_SIZE"]
    with open(payload_file, "rb") as fff:
        fff.seek(offset)
        payload = bytearray(fff.read())
    rem_len = len(payload) % padsize
    debug_print(
        f"GetPayload: padsize={padsize:0x} len(payload)={len(payload):0x} rem={rem_len:0x}"
    )

    if rem_len:
        payload += chip_dict["PAYLOAD_PAD_BYTE"] * (padsize - rem_len)
        debug_print(f"GetPayload: Added {padsize - rem_len} padding bytes")

    return payload


def GetPayload(payload_file, chip_dict):  # pylint:disable=invalid-name
    """Read payload and pad it to chip_dict["PAD_SIZE"]"""
    return GetPayloadFromOffset(payload_file, 0, chip_dict)


def GetPublicKey(pem_file):  # pylint:disable=invalid-name
    """Extract public exponent and modulus from PEM file."""
    result = subprocess.run(
        ["openssl", "rsa", "-in", pem_file, "-text", "-noout"],
        stdout=subprocess.PIPE,
        encoding="utf-8",
        check=True,
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


def GetSpiClockParameter(args):  # pylint:disable=invalid-name
    """Returns the spi clock parameter."""
    assert (
        args.spi_clock in SPI_CLOCK_LIST
    ), f"Unsupported SPI clock speed {args.spi_clock:d} MHz"
    return SPI_CLOCK_LIST.index(args.spi_clock)


def GetSpiReadCmdParameter(args):  # pylint:disable=invalid-name
    """Returns the spi read cmd."""
    assert (
        args.spi_read_cmd in SPI_READ_CMD_LIST
    ), f"Unsupported SPI read command 0x{args.spi_read_cmd:x}"
    return SPI_READ_CMD_LIST.index(args.spi_read_cmd)


def GetEncodedSpiDriveStrength(args):  # pylint:disable=invalid-name
    """Returns the encoded spi drive strength."""
    assert (
        args.spi_drive_str in SPI_DRIVE_STR_DICT
    ), f"Unsupported SPI drive strength {args.spi_drive_str:d} mA"
    return SPI_DRIVE_STR_DICT.get(args.spi_drive_str)


def GetSpiSlewRate(args):  # pylint:disable=invalid-name
    """Return 0=Slow slew rate or 1=Fast slew rate"""
    if args.spi_slew_fast:
        return 1
    return 0


def GetSpiCpol(args):  # pylint:disable=invalid-name
    """Return SPI CPOL = 0 or 1"""
    if args.spi_cpol == 0:
        return 0
    return 1


def GetSpiCphaMosi(args):  # pylint:disable=invalid-name
    """Return SPI CPHA_MOSI
    0 = SPI Master drives data is stable on inactive to clock edge
    1 = SPI Master drives data is stable on active to inactive clock edge
    """
    if args.spi_cpha_mosi == 0:
        return 0
    return 1


def GetSpiCphaMiso(args):  # pylint:disable=invalid-name
    """Return SPI CPHA_MISO 0 or 1
    0 = SPI Master samples data on inactive to active clock edge
    1 = SPI Master samples data on active to inactive clock edge
    """
    if args.spi_cpha_miso == 0:
        return 0
    return 1


def PadZeroTo(data, size):  # pylint:disable=invalid-name
    """Pads zeros on data."""
    data.extend(b"\0" * (size - len(data)))


def BuildHeader2(
    args, chip_dict, payload_len, load_addr, payload_entry
):  # pylint:disable=invalid-name
    """Build SPI image header for MEC172x
    MEC172x image header size = 320(0x140) bytes

    Description using Python slice notation [start:start+len]

    header[0:4] = 'PHCM'
    header[4] = header version = 0x03(MEC172x)
    header[5] = SPI clock speed, drive strength, sampling mode
    bits[1:0] = SPI clock speed: 0=48, 1=24, 2=16, 3=12
    bits[3:2] = SPI controller pins drive strength
                00b=2mA, 01b=4mA, 10b=8mA, 11b=12mA
    bit[4] = SPI controller pins slew rate: 0=slow, 1=fast
    bit[5] = SPI CPOL: 0=SPI clock idle is low, 1=idle is high
    bit[6] = CHPHA_MOSI
        1:data change on first inactive to active clock edge
        0:data change on first active to inactive clock edge
    bit[7] = CHPHA_MISO:
        1: Data captured on first inactive to active clock edge
        0: Data captured on first active to inactive clock edge
    header[6] Boot-ROM loader flags
    bits[0] = 0(use header[5] b[1:0]), 1(96 MHz)
    bits[2:1] = 0 reserved
    bits[5:3] = 111b
    bit[6]: For MEC172x controls authentication
            0=Authentication disabled. Signature is SHA-384 of FW payload
            1=Authentication enabled. Signature is ECDSA P-384
    bit[7]: 0=FW pyload not encrypted, 1=FW payload is encrypted
    header[7]: SPI Flash read command
            0 -> 0x03 1-1-1 read freq < 33MHz
            1 -> 0x0B 1-1-1 + 8 clocks(data tri-stated)
            2 -> 0x3B 1-1-2 + 8 clocks(data tri-stated). Data phase is dual I/O
            3 -> 0x6B 1-1-4 + 8 clocks(data tri-stated). Data phase is Quad I/O
            NOTE: Quad requires SPI flash device QE(quad enable) bit
            to be factory set. Enabling QE disables HOLD# and WP#
            functionality of the SPI flash device.
    header[0x8:0xC] SRAM Load address little-endian format
    header[0xC:0x10] SRAM FW entry point. Boot-ROM jumps to
                    this address on successful load. (little-endian)
    header[0x10:0x12] little-endian format: FW binary size in units of
                    128 bytes(MEC172x)
    header[0x12:0x14] = 0 reserved
    header[0x14:0x18] = Little-ending format: Unsigned offset from start of
                        header to FW payload.
                        MEC172x: Offset must be a multiple of 128
                        Offset must be >= header size.
                        NOTE: If Authentication is enabled size includes
                        the appended signature.
    MEC172x:
    header[0x18] = Authentication key select. Set to 0 for no Authentication.
    header[0x19] = SPI flash device(s) drive strength feature flags
                    b[0]=1 SPI flash device 0 has drive strength feature
                    b[1]=SPI flash 0: DrvStr Write format(0=2 bytes, 1=1 byte)
                    b[2]=1 SPI flash device 1 has drive strength feature
                    b[3]=SPI flash 1: DrvStr Write format(0=2 bytes, 1=1 byte)
                    b[7:4] = 0 reserved
    header[0x1A:0x20] = 0 reserved
    header[0x20] = SPI opcode to read drive strength from SPI flash 0
    header[0x21] = SPI opcode to write drive strength to SPI flash 0
    header[0x22] = SPI flash 0: drvStr value
    header[0x23] = SPI flash 0: drvStr mask
    header[0x24] = SPI opcode to read drive strength from SPI flash 1
    header[0x25] = SPI opcode to write drive strength to SPI flash 1
    header[0x26] = SPI flash 1: drvStr value
    header[0x27] = SPI flash 1: drvStr mask
    header[0x28:0x48] = reserved, may be any value
    header[0x48:0x50] = reserved for customer use
    header[0x50:0x80] = ECDSA-384 public key x-coord. = 0 Auth. disabled
    header[0x80:0xB0] = ECDSA-384 public key y-coord. = 0 Auth. disabled
    header[0xB0:0xE0] = SHA-384 digest of header[0:0xB0]
    header[0xE0:0x110] = Header ECDSA-384 signature x-coord. = 0 Auth. disabled
    header[0x110:0x140] = Header ECDSA-384 signature y-coor. = 0 Auth. disabled
    """
    header_size = chip_dict["HEADER_SIZE"]

    # allocate zero filled header
    header = bytearray(b"\x00" * header_size)
    debug_print("len(header) = ", len(header))

    # Identifier and header version
    header[0:4] = b"PHCM"
    header[4] = chip_dict["HEADER_VER"]

    # SPI frequency, drive strength, CPOL/CPHA encoding same for both chips
    spi_freq_index = GetSpiClockParameter(args)
    if spi_freq_index > 3:
        header[6] |= 0x01
    else:
        header[5] = spi_freq_index

    header[5] |= (GetEncodedSpiDriveStrength(args) & 0x03) << 2
    header[5] |= (GetSpiSlewRate(args) & 0x01) << 4
    header[5] |= (GetSpiCpol(args) & 0x01) << 5
    header[5] |= (GetSpiCphaMosi(args) & 0x01) << 6
    header[5] |= (GetSpiCphaMiso(args) & 0x01) << 7

    # header[6]
    # b[0] value set above
    # b[2:1] = 00b, b[5:3]=111b
    # b[7]=0 No encryption of FW payload
    header[6] |= 0x7 << 3

    # SPI read command set same for both chips
    header[7] = GetSpiReadCmdParameter(args) & 0xFF

    # bytes 0x08 - 0x0b
    header[0x08:0x0C] = load_addr.to_bytes(4, byteorder="little")
    # bytes 0x0c - 0x0f
    header[0x0C:0x10] = payload_entry.to_bytes(4, byteorder="little")

    # bytes 0x10 - 0x11 payload length in units of 128 bytes
    assert payload_len % chip_dict["PAYLOAD_GRANULARITY"] == 0, print(
        f"Payload size not a multiple of {chip_dict['PAYLOAD_GRANULARITY']}"
    )

    payload_units = int(payload_len // chip_dict["PAYLOAD_GRANULARITY"])
    assert payload_units < 0x10000, print(
        f"Payload too large: len={payload_len} units={payload_units}"
    )

    header[0x10:0x12] = payload_units.to_bytes(2, "little")

    # bytes 0x14 - 0x17 TODO offset from start of payload to FW payload to be
    # loaded by Boot-ROM. We ask Boot-ROM to load (LFW || EC_RO).
    # LFW location provided on the command line.
    assert args.lfw_loc % 4096 == 0, print(
        f"LFW location not on a 4KB boundary! 0x{args.lfw_loc:0x}"
    )

    assert args.lfw_loc >= (args.header_loc + chip_dict["HEADER_SIZE"]), print(
        "LFW location not greater than header location + header size"
    )

    lfw_ofs = args.lfw_loc - args.header_loc
    header[0x14:0x18] = lfw_ofs.to_bytes(4, "little")

    # MEC172x: authentication key select. Authentication not used, set to 0.
    header[0x18] = 0

    # header[0x19], header[0x20:0x28]
    # header[0x1A:0x20] reserved 0
    # MEC172x: supports SPI flash devices with drive strength settings
    # TODO leave these fields at 0 for now. We must add 6 command line
    # arguments.

    # header[0x28:0x48] reserve can be any value
    # header[0x48:0x50] Customer use. TODO
    # authentication disabled, leave these 0.
    # header[0x50:0x80] ECDSA P384 Authentication Public key Rx
    # header[0x80:0xB0] ECDSA P384 Authentication Public key Ry

    # header[0xB0:0xE0] = SHA384(header[0:0xB0])
    header[0xB0:0xE0] = hashlib.sha384(header[0:0xB0]).digest()
    # When ECDSA authentication is disabled MCHP SPI image generator
    # is filling the last 48 bytes of the Header with 0xff
    header[-48:] = b"\xff" * 48

    debug_print("After hash: len(header) = ", len(header))

    return header


def GenEcInfoBlock(_args, chip_dict):  # pylint:disable=invalid-name
    """MEC172x 128-byte EC Info Block appended to end of padded FW binary.
    Python slice notation
    byte[0:0x64] are undefined, we set to 0xFF
    byte[0x64] = FW Build LSB
    byte[0x65] = FW Build MSB
    byte[0x66:0x68] = undefined, set to 0xFF
    byte[0x68:0x78] = Roll back permissions bit maps
    byte[0x78:0x7c] = key revocation bit maps
    byte[0x7c] = platform ID LSB
    byte[0x7d] = platform ID MSB
    byte[0x7e] = auto-rollback protection enable
    byte[0x7f] = current imeage revision
    """
    # ecinfo = bytearray([0xff] * chip_dict["EC_INFO_BLK_SZ"])
    ecinfo = bytearray(chip_dict["EC_INFO_BLK_SZ"])
    return ecinfo


def GenCoSignature(_args, chip_dict, _payload):  # pylint:disable=invalid-name
    """Generate SPI FW image co-signature.
    MEC172x cosignature is 96 bytes used by OEM FW
    developer to sign their binary with ECDSA-P384-SHA384 or
    some other signature algorithm that fits in 96 bytes.
    At this time Cros-EC is not using this field, fill with 0xFF.
    If this feature is implemented we need to read the OEM's
    generated signature from a file and extract the binary
    signature.
    """
    return bytearray(b"\xff" * chip_dict["COSIG_SZ"])


def GenTrailer(  # pylint:disable=invalid-name
    _args, chip_dict, payload, encryption_key_header, ec_info_block, cosignature
):
    """Generate SPI FW Image trailer.
    MEC172x: Size = 160 bytes
    binary = payload || encryption_key_header || ec_info_block || cosignature
    trailer[0:48] = SHA384(binary)
    trailer[48:144] = 0xFF
    trailer[144:160] = 0xFF. Boot-ROM spec. says these bytes should be random.
        Authentication & encryption are not used therefore random data
        is not necessary.
    """
    debug_print("GenTrailer SHA384 computation")
    trailer = bytearray(chip_dict["TAILER_PAD_BYTE"] * chip_dict["TRAILER_SZ"])
    hasher = hashlib.sha384()
    hasher.update(payload)
    debug_print(f"  Update: payload len=0x{len(payload):0x}")
    if ec_info_block is not None:
        hasher.update(ec_info_block)
        debug_print(f"  Update: ec_info_block len=0x{len(ec_info_block):0x}")
    if encryption_key_header is not None:
        hasher.update(encryption_key_header)
        debug_print(
            f"  Update: encryption_key_header len=0x{len(encryption_key_header):0x}"
        )
    if cosignature is not None:
        hasher.update(cosignature)
        debug_print(f"  Update: cosignature len=0x{len(cosignature):0x}")
    trailer[0:48] = hasher.digest()
    trailer[-16:] = 16 * b"\xff"

    return trailer


def BuildTag(args):  # pylint:disable=invalid-name
    """MEC172x supports two 32-bit Tags located at offsets 0x0 and 0x4
    in the SPI flash.
    Tag format:
    bits[23:0] correspond to bits[31:8] of the Header SPI address
        Header is always on a 256-byte boundary.
    bits[31:24] = CRC8-ITU of bits[23:0].
    Notice there is no chip-select field in the Tag both Tag's point
    to the same flash part.
    """
    tag = bytearray(
        [
            (args.header_loc >> 8) & 0xFF,
            (args.header_loc >> 16) & 0xFF,
            (args.header_loc >> 24) & 0xFF,
        ]
    )
    tag.append(Crc8(0, tag))
    return tag


def BuildTagFromHdrAddr(header_loc):  # pylint:disable=invalid-name
    """Builds a tag from a header address."""
    tag = bytearray(
        [
            (header_loc >> 8) & 0xFF,
            (header_loc >> 16) & 0xFF,
            (header_loc >> 24) & 0xFF,
        ]
    )
    tag.append(Crc8(0, tag))
    return tag


def BuildFlashMap(secondSpiFlashBaseAddr):  # pylint:disable=invalid-name
    """FlashMap is an option for MEC172x
    It is a 32 bit structure
    bits[18:0] = bits[30:12] of second SPI flash base address
    bits[23:19] = 0 reserved
    bits[31:24] = CRC8 of bits[23:0]
    Input:
    integer containing base address of second SPI flash
    This value is usually equal to the size of the first
    SPI flash and should be a multiple of 4KB
    Output:
    bytearray of length 4
    """
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
def PacklfwRoImage(
    rorw_file, loader_file, image_size
):  # pylint:disable=invalid-name
    """Create a temp file with the
    first image_size bytes from the loader file and append bytes
    from the rorw file.
    return the filename"""
    with tempfile.NamedTemporaryFile(
        delete=False
    ) as outfile:  # Need to keep file around
        with open(loader_file, "rb") as fin1:  # read 4KB loader file
            pro = fin1.read()
        outfile.write(pro)  # write 4KB loader data to temp file
        with open(rorw_file, "rb") as fin:
            readbytes = fin.read(image_size)

        outfile.write(readbytes)

        return outfile.name


def gen_test_ecrw(pldrw):
    """Generate a test EC_RW image of same size
    as original.
    Preserve image_data structure and fill all
    other bytes with 0xA5.
    useful for testing SPI read and EC build
    process hash generation.
    """
    debug_print("gen_test_ecrw: pldrw type =", type(pldrw))
    debug_print("len pldrw =", len(pldrw), " = ", hex(len(pldrw)))
    cookie1_pos = pldrw.find(b"\x99\x88\x77\xce")
    cookie2_pos = pldrw.find(b"\xdd\xbb\xaa\xce", cookie1_pos + 4)
    ttt = struct.unpack("<L", pldrw[cookie1_pos + 0x24 : cookie1_pos + 0x28])
    size = ttt[0]
    debug_print("EC_RW size =", size, " = ", hex(size))

    debug_print("Found cookie1 at ", hex(cookie1_pos))
    debug_print("Found cookie2 at ", hex(cookie2_pos))

    if cookie2_pos > cookie1_pos > 0:
        for i in range(0, cookie1_pos):
            pldrw[i] = 0xA5
        for i in range(cookie2_pos + 4, len(pldrw)):
            pldrw[i] = 0xA5

    with open("ec_RW_test.bin", "wb") as fecrw:
        fecrw.write(pldrw[:size])


def parseargs():
    """Parses command line arguments."""
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
        "--load_addr", type=int, help="EC SRAM load address", default=0xC0000
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
        help="Location of header in SPI flash. Must be on a 256 byte boundary",
        default=0x0100,
    )
    parser.add_argument(
        "--lfw_loc",
        type=int,
        help="Location of LFW in SPI flash. Must be on a 4KB boundary",
        default=0x1000,
    )
    parser.add_argument(
        "--lfw_size", type=int, help="LFW size in bytes", default=0x1000
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
        help="SPI read command. 0x3, 0xB, 0x3B, or 0x6B.",
        default=0xB,
    )
    parser.add_argument(
        "--image_size",
        type=int,
        help="Size of a single image. Default 244KB",
        default=(244 * 1024),
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
        "--tag0_loc", type=int, help="MEC172x TAG0 SPI offset", default=0
    )
    parser.add_argument(
        "--tag1_loc", type=int, help="MEC172x TAG1 SPI offset", default=4
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
        help="""SPI clock phase controller drives data.
                              0=Data driven on active to inactive clock edge,
                              1=Data driven on inactive to active clock edge""",
        default=0,
    )
    parser.add_argument(
        "--spi_cpha_miso",
        type=int,
        help="""SPI clock phase controller samples data.
                              0=Data sampled on inactive to active clock edge,
                              1=Data sampled on active to inactive clock edge""",
        default=0,
    )

    return parser.parse_args()


def print_args(args):
    """Prints all the command ling arguments."""
    debug_print("parsed arguments:")
    debug_print(".input  = ", args.input)
    debug_print(".output = ", args.output)
    debug_print(".loader_file = ", args.loader_file)
    debug_print(".spi_size (KB) = ", hex(args.spi_size))
    debug_print(".image_size = ", hex(args.image_size))
    debug_print(".load_addr", hex(args.load_addr))
    debug_print(".tag0_loc = ", hex(args.tag0_loc))
    debug_print(".tag1_loc = ", hex(args.tag1_loc))
    debug_print(".header_loc = ", hex(args.header_loc))
    debug_print(".lfw_loc = ", hex(args.lfw_loc))
    debug_print(".lfw_size = ", hex(args.lfw_size))
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


def spi_list_append(mylist, loc, data, description):
    """Append SPI data block tuple to list"""
    mylist.append((loc, data, description))
    debug_print(
        f"Add SPI entry: offset=0x{loc:08x} len=0x{len(data):0x} descr={description}"
    )


#
# Handle quiet mode build from Makefile
# Quiet mode when V is unset or V=0
# Verbose mode when V=1
#
# MEC172x SPI Image Generator
# No authentication
# No payload encryption
#
# SPI Offset 0x0 = TAG0 points to Header for EC-RO FW
# SPI Offset 0x4 = TAG1 points to Header for EC-RO FW
# TAG Size = 4 bytes
#  bits[23:0] = bits[31:8] of Header SPI offset
#  bits[31:24] = CRC8-ITU checksum of bits[23:0].
#
# MEC172x SPI header and payload layout for minimum size
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
    """Main function."""
    global debug_print  # pylint:disable=global-statement,invalid-name

    args = parseargs()

    if args.verbose:
        debug_print = print

    debug_print("Begin pack_ec_mec172x.py script")

    print_args(args)

    chip_dict = MEC172X_DICT

    # Boot-ROM requires header location aligned >= 256 bytes.
    # CrOS EC flash image update code requires EC_RO/RW location to be aligned
    # on a flash erase size boundary and EC_RO/RW size to be a multiple of
    # the smallest flash erase block size.

    spi_size = args.spi_size * 1024

    rorofile = PacklfwRoImage(args.input, args.loader_file, args.image_size)
    debug_print("Temporary file containing LFW + EC_RO is ", rorofile)

    lfw_ecro = GetPayload(rorofile, chip_dict)
    lfw_ecro_len = len(lfw_ecro)
    debug_print("Padded LFW + EC_RO length = ", hex(lfw_ecro_len))

    # SPI test mode compute CRC32 of EC_RO and store in last 4 bytes
    if args.test_spi:
        crc32_ecro = zlib.crc32(bytes(lfw_ecro[args.lfw_size : -4]))
        crc32_ecro_bytes = crc32_ecro.to_bytes(4, byteorder="little")
        lfw_ecro[-4:] = crc32_ecro_bytes
        debug_print("ecro len = ", hex(len(lfw_ecro) - args.lfw_size))
        debug_print("CRC32(ecro-4) = ", hex(crc32_ecro))

    # Reads entry point from offset 4 of file.
    # This assumes binary has Cortex-M4 vector table at offset 0.
    # 32-bit word at offset 0x0 initial stack pointer value
    # 32-bit word at offset 0x4 address of reset handler
    # NOTE: reset address will have bit[0]=1 to ensure thumb mode.
    lfw_ecro_entry = GetEntryPoint(rorofile)
    debug_print(f"LFW Entry point from GetEntryPoint = 0x{lfw_ecro_entry:08x}")

    # Chromebooks are not using MEC BootROM SPI header/payload authentication
    # or payload encryption. In this case the header authentication signature
    # is filled with the hash digest of the respective entity.
    # BuildHeader2 computes the hash digest and stores it in the correct
    # header location.
    header = BuildHeader2(
        args, chip_dict, lfw_ecro_len, args.load_addr, lfw_ecro_entry
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

    ecrw = GetPayloadFromOffset(args.input, args.image_size, chip_dict)
    debug_print("type(ecrw) is ", type(ecrw))
    debug_print("len(ecrw) is ", hex(len(ecrw)))

    # truncate to args.image_size
    ecrw_len = len(ecrw)
    if ecrw_len > args.image_size:
        debug_print(
            f"Truncate EC_RW len={ecrw_len:0x} to image_size={args.image_size:0x}"
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

    spi_list = []

    # MEC172x Add TAG's
    # spi_list.append((args.tag0_loc, tag0, "tag0"))
    # spi_list.append((args.tag1_loc, tag1, "tag1"))
    spi_list_append(spi_list, args.tag0_loc, tag0, "TAG0")
    spi_list_append(spi_list, args.tag1_loc, tag1, "TAG1")

    # Boot-ROM SPI image header for LFW+EC-RO
    # spi_list.append((args.header_loc, header, "header(lfw + ro)"))
    spi_list_append(spi_list, args.header_loc, header, "LFW-EC_RO Header")

    spi_list_append(spi_list, args.lfw_loc, lfw_ecro, "LFW-EC_RO FW")

    offset = args.lfw_loc + len(lfw_ecro)
    debug_print(f"SPI offset after LFW_ECRO = 0x{offset:08x}")

    if ec_info_block is not None:
        spi_list_append(spi_list, offset, ec_info_block, "LFW-EC_RO Info Block")
        offset += len(ec_info_block)

    debug_print(f"SPI offset after ec_info_block = 0x{offset:08x}")

    if cosignature is not None:
        # spi_list.append((offset, co-signature, "ECRO Co-signature"))
        spi_list_append(spi_list, offset, cosignature, "LFW-EC_RO Co-signature")
        offset += len(cosignature)

    debug_print(f"SPI offset after co-signature = 0x{offset:08x}")

    if trailer is not None:
        # spi_list.append((offset, trailer, "ECRO Trailer"))
        spi_list_append(spi_list, offset, trailer, "LFW-EC_RO trailer")
        offset += len(trailer)

    debug_print(f"SPI offset after trailer = 0x{offset:08x}")

    # EC_RW location
    rw_offset = int(spi_size // 2)
    if args.rw_loc >= 0:
        rw_offset = args.rw_loc

    debug_print(f"rw_offset = 0x{rw_offset:08x}")

    # spi_list.append((rw_offset, ecrw, "ecrw"))
    spi_list_append(spi_list, rw_offset, ecrw, "EC_RW")
    offset = rw_offset + len(ecrw)

    spi_list = sorted(spi_list)

    debug_print("Display spi_list:")
    dumpsects(spi_list)

    #
    # MEC172x Boot-ROM locates TAG0/1 at SPI offset 0
    # instead of end of SPI.
    #
    with open(args.output, "wb") as output_file:
        debug_print("Write spi list to file", args.output)
        addr = 0
        for loc, data, _ in spi_list:
            if addr < loc:
                debug_print(
                    "Offset ",
                    hex(addr),
                    " Length",
                    hex(loc - addr),
                    "fill with 0xff",
                )
                output_file.write(b"\xff" * (loc - addr))
                addr = loc
                debug_print(
                    "Offset ",
                    hex(addr),
                    " Length",
                    hex(len(data)),
                    "write data",
                )

            output_file.write(data)
            addr += len(data)

        if addr < spi_size:
            debug_print(
                "Offset ",
                hex(addr),
                " Length",
                hex(spi_size - addr),
                "fill with 0xff",
            )
            output_file.write(b"\xff" * (spi_size - addr))

        output_file.flush()


if __name__ == "__main__":
    main()
