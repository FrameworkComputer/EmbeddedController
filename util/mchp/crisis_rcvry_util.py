#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""

Script to program a Microchip EC with erased or a bricked flash.

General usage:
1. Assert UART crisis recovery pin and toggle EC reset
2. Set baud rate of serial port on host to 9600
3. Download and execute Second Loader into SRAM:
    crisis_rcvry_util.py -s <mchp-loader-image> -p <serial-port>
4. Set baud rate of serial port on host to 57600
5. Download and program new EC images into flash:
    crisis_rcvry_util.py -e <ec-image> -p <serial-port>
6. De-assert UART crisis recovery pin, and toggle EC reset
7. EC boots programmed image
8. Set baud rate of serial port on host to match EC image configuration

Refer to the Microchip Crisis_Mode-cmdSequence document for full details.
"""

import argparse
import binascii
from dataclasses import dataclass
import enum
import logging
import os
import struct
import sys
import time

import serial  # pylint:disable=import-error


CRC_LENGTH = 4

HEADER_WRITE_CMD_ID = 0x65
FW_IMAGE_WRITE_CMD_ID = 0x67
SRAM_EXE_CMD_ID = 0x69
EC_FW_TRANS_WAIT_BYTE = 262144
IMAGE_CHUNK_LENGTH = 128
HEADER_CHUNK_LENGTH = 64
HEADER_LENGTH = 320
START_OFFSET = 0
RESPONSE_LENGTH = 5
RETRY_COUNT = 3
DELAY_50_MS = 0.05
IMG_LEN_ENCRYPTION = 512
IMG_LEN_NO_ENCRYPTION = 384
RESPONSE_STATUS_IMAGE_INVA_MASK = 0x06
RESPONSE_PACKET_ERROR_MASK = 0x80
ACK_TIMEOUT_SECS = 22

HOST_NEGO_CMD_SEQ = b"\x55\xAA"
HOST_NEGO_CMD_RESP_SEQ = b"\x5A\xA5"
FLASH_PGM_CMD_SEQ = b"\x33\xCC"
FLASH_PGM_CMD_RESP_SEQ = b"\x3C\xC3"

VENDOR_ID = b"MCHP"
PGM_HEADER = b"\x01\x08"
PRG_HEADER_LEN = b"\x08"
TERMINATOR_ID = b"XEOF"
PRG_HDR_TOTAL_LEN = 0xF0

MCHP_VENDOR_ID_START = 0
MCHP_VENDOR_ID_END = len(VENDOR_ID)
MCHP_PGM_HDR_START = 6
MCHP_PGM_HDR_END = MCHP_PGM_HDR_START + len(PGM_HEADER)
MCHP_PGM_HDR_LEN_START = 13
MCHP_PGM_HDR_LEN_END = MCHP_PGM_HDR_LEN_START + len(PRG_HEADER_LEN)
MCHP_TERM_ID_START = PRG_HDR_TOTAL_LEN - 8
MCHP_TERM_ID_END = MCHP_TERM_ID_START + len(TERMINATOR_ID)


logging.basicConfig(level=logging.INFO)


class Transfer(enum.Enum):
    """A class for holding transfer type"""

    SECOND_LOADER_HDR = 1
    SECOND_LOADER_FW = 2
    EC_HDR = 3
    EC_FW = 4


@dataclass
class PacketProperties:
    """A class for holding transfer method argument"""

    uart_handle: int
    command_id: int
    max_write_length: int
    payload_length: int
    payload: bytearray
    transfer_type: enum.Enum


def update_progress(curr_val, max_val):
    """Updates the header/firmware transfer status in console.

    Args:
        curr_val: Size currently transferred
        max_val: Maximum size needs to be transferred
    """
    print(f"Transfer {curr_val} of {max_val}.", end="\r")


def response_bytes(command_id):
    """Returns the response bytes for the FW IMAGE WRITE CMD.

    Args:
        command_id: Given command ID

    Returns:
        resp_bytes: Response byte for the given command ID
    """
    resp_bytes = bytearray()

    resp_bytes.append(command_id)
    resp_bytes.extend(get_crc_bytes(resp_bytes))

    return resp_bytes


def ec_image_transfer(uart_handle, ec_file):
    """Transfer EC image into the internal flash.

    Args:
        uart_handle: Handle of the UART port
        ec_file: EC image file path
    """

    header_gen = bytearray(PRG_HDR_TOTAL_LEN)

    header_gen[MCHP_VENDOR_ID_START:MCHP_VENDOR_ID_END] = VENDOR_ID
    header_gen[MCHP_PGM_HDR_START:MCHP_PGM_HDR_END] = PGM_HEADER
    header_gen[MCHP_PGM_HDR_LEN_START:MCHP_PGM_HDR_LEN_END] = PRG_HEADER_LEN
    header_gen[MCHP_TERM_ID_START:MCHP_TERM_ID_END] = TERMINATOR_ID

    logging.info("EC program header transfer in progress")
    trans_prop = PacketProperties(
        uart_handle,
        HEADER_WRITE_CMD_ID,
        PRG_HDR_TOTAL_LEN,
        PRG_HDR_TOTAL_LEN,
        header_gen,
        Transfer.EC_HDR,
    )
    transfer(trans_prop)
    logging.info("EC program header transfer completed")

    logging.info("EC FW image transfer in progress")
    with open(ec_file, mode="rb") as data:
        image = data.read()
    max_len = os.path.getsize(ec_file)

    trans_prop = PacketProperties(
        uart_handle,
        FW_IMAGE_WRITE_CMD_ID,
        IMAGE_CHUNK_LENGTH,
        max_len,
        image,
        Transfer.EC_FW,
    )
    transfer(trans_prop)
    logging.info("EC FW image transfer completed")


def transfer(prop_obj):
    """Transfer the payload with the command sequence.

    Args:
        prop_obj.uart_handle: Handle of the UART port
        prop_obj.command_id: Given command ID
        prop_obj.max_write_length: Maximum number of bytes
                 written to the UART during a single write
        prop_obj.payload_length: Total length of the payload
        prop_obj.payload: Payload bytes to write
        prop_obj.bootloader_active: Differentiate whether
                 second loader or bootloader is active in EC.
    """

    resp_bytes = response_bytes(prop_obj.command_id)

    offset = START_OFFSET
    retry_count = RETRY_COUNT
    while offset < prop_obj.payload_length:
        update_progress(offset, prop_obj.payload_length)

        program_length = min(
            prop_obj.max_write_length, prop_obj.payload_length - offset
        )
        file_content = prop_obj.payload[offset : offset + program_length]
        cmd_bytes = construct_write_packet(
            prop_obj.command_id, program_length, offset, file_content
        )

        if cmd_bytes is None:
            sys.exit(1)

        prop_obj.uart_handle.write(cmd_bytes)

        if prop_obj.transfer_type in (
            Transfer.SECOND_LOADER_HDR,
            Transfer.SECOND_LOADER_FW,
        ):
            # Bootloader need some time to generate response
            # It runs slower than the Second loader that execute from SRAM
            time.sleep(DELAY_50_MS)

        prop_obj.uart_handle.reset_input_buffer()

        # Second loader buffers the received bytes till reaches 256Kbytes.
        # Once it reaches, second loader transfer it to FLASH
        # Required some delay to complete this transfer.
        if prop_obj.transfer_type == Transfer.EC_FW:
            byte_transferred = offset + program_length
            if (byte_transferred >= prop_obj.payload_length) or (
                byte_transferred == EC_FW_TRANS_WAIT_BYTE
            ):
                logging.info(
                    "Waiting for EC to program. Will take about %ss.",
                    ACK_TIMEOUT_SECS,
                )
                time.sleep(ACK_TIMEOUT_SECS)

        rf_cont_disp = prop_obj.uart_handle.read(RESPONSE_LENGTH)

        # Success response:
        # Size: 5Bytes
        #    0th byte -> Rxd Command
        #    1st to 4th bytes -> CRC

        # Failure response:
        # (Error has been detected in the last received packet(Host->EC) by EC)
        # Size: 6Bytes
        #    0th byte -> Rxd Command | 0x80
        #    1st byte -> Status byte
        #        0th bit -> CRC failure
        #        1st bit -> Incorrect payload length
        #        2nd bit -> Incorrect header offset
        #    2nd to 5th bytes -> CRC

        if rf_cont_disp != resp_bytes:
            # Less than RESPONSE_LENGTH could be because of read timeout
            if len(rf_cont_disp) < RESPONSE_LENGTH:
                logging.critical("Timeout Error")
                sys.exit(1)
            elif rf_cont_disp[0] & RESPONSE_PACKET_ERROR_MASK:
                # Check for Host -> EC packet error
                # Since its Failure response, read one more byte.
                rf_cont_disp.append(prop_obj.uart_handle.read(1))
                # Check status for Incorrect payload length or header offset
                if rf_cont_disp[1] & RESPONSE_STATUS_IMAGE_INVA_MASK:
                    logging.critical("Image not valid")
                    sys.exit(1)

            # CRC failure
            retry_count = retry_count - 1
            if retry_count == 0:
                logging.critical("CRC mis-match observed")
                sys.exit(1)
            logging.info("CRC mis-match on the last transfer, retrying")
            continue

        offset = offset + program_length


def second_loader_image_transfer(uart_handle, spi_file):
    """Transfer the second loader image into SRAM.

    Args:
        uart_handle: Handle of the UART port
        spi_file: Given Second Loader image file
    """
    tag_base_addr = 0
    with open(spi_file, mode="rb") as data:
        image = data.read()

    # Extracting the content
    addr = (struct.unpack("<I", image[0:4]))[0]
    addr &= 0xFFFFFF
    if addr != 0xFFFFFF:
        tag_base_addr = addr << 8

    hdr_addr = tag_base_addr
    header_len = HEADER_LENGTH
    img_offset = hdr_addr + header_len
    hdr_file_data = image[tag_base_addr : tag_base_addr + header_len]
    encryption_enable = struct.unpack("B", hdr_file_data[0x06:0x07])[0]
    img_len = (
        struct.unpack("<H", hdr_file_data[0x10:0x12])[0]
    ) * IMAGE_CHUNK_LENGTH
    if encryption_enable == 0x80:
        img_len = int(img_len) + IMG_LEN_ENCRYPTION - 16
    else:
        img_len = int(img_len) + IMG_LEN_NO_ENCRYPTION - 16

    logging.info("Second loader header transfer in progress")

    trans_prop = PacketProperties(
        uart_handle,
        HEADER_WRITE_CMD_ID,
        HEADER_CHUNK_LENGTH,
        header_len,
        image[hdr_addr : hdr_addr + header_len],
        Transfer.SECOND_LOADER_HDR,
    )
    transfer(trans_prop)
    logging.info("Second loader header transfer completed")

    logging.info("Second loader FW image transfer in progress")
    trans_prop = PacketProperties(
        uart_handle,
        FW_IMAGE_WRITE_CMD_ID,
        IMAGE_CHUNK_LENGTH,
        img_len,
        image[img_offset : img_offset + img_len],
        Transfer.SECOND_LOADER_FW,
    )
    transfer(trans_prop)
    logging.info("Second loader FW image transfer completed")


def get_crc_bytes(_bytes):
    """The below routine will calculate CRC for given

    Args:
        _bytes: Given bytes

    Returns:
        crc_bytes: integer to bytes of crc32
    """
    crc_data = binascii.crc32(_bytes)
    crc_bytes = convert_crc_int_to_bytes(crc_data)
    return crc_bytes


def convert_crc_int_to_bytes(integer_crc_data):
    """The below function will convert the integer crc to
        byte array format

    Args:
        integer_crc_data: integer value

    Returns:
        crc_bytes: computed crc for the given data
        e.g. Input: Integer crc = 0xb016f118 will return
        bytearray[4] = {0x18, 0xf1, 0x16, 0xb0}
    """
    crc_bytes = bytearray()
    for _ in range(CRC_LENGTH):
        data = integer_crc_data & 0xFF
        crc_bytes.append(data)
        integer_crc_data = integer_crc_data >> 8
    return crc_bytes


def construct_write_packet(command, length, offset, data_bytes):
    """Construct the byte array Packet start with
    command, length, offset, data_bytes and CRC

    Args:
        command: Identifier for the current packet
        length: Length of the packet, excluding command
        offset: packet offset address
        data_bytes: packet payload

    Returns:
        cmd_byte_seq: Packet as byte array
    """
    if length < 1:
        logging.critical("Invalid length to construct write packet")
        return None

    cmd_bytes_seq = bytearray()
    cmd_bytes_seq.append(command)
    cmd_bytes_seq.append(length)
    cmd_bytes_seq.append(offset & 0xFF)
    cmd_bytes_seq.append((offset >> 8) & 0xFF)
    cmd_bytes_seq.append((offset >> 16) & 0xFF)
    cmd_bytes_seq.extend(data_bytes)
    cmd_bytes_seq.extend(get_crc_bytes(cmd_bytes_seq))
    return cmd_bytes_seq


def command_write(uart_handle, cmd_seq, resp_seq):
    """flash program command is used to establish communication

    Args:
        uart_handle: Handle of the UART port
        cmd_seq: Sequence of command bytes to transfer
        resp_seq: Expected response sequence

    Returns:
        Compares the received response against the expected one
        and returns True/False
    """
    uart_handle.reset_input_buffer()
    uart_handle.write(cmd_seq)

    i = 0
    while i < RETRY_COUNT:
        read = uart_handle.read(2)
        if len(read) == 0:
            i += 1
        elif read == resp_seq:
            return True
        else:
            break

    return False


def req_ec_to_run_second_loader(uart_handle):
    """Send SRAM EXE command to BROM to run the second loader image from SRAM

    Args:
        uart_handle: Handle of the UART port
    """
    cmd_bytes = bytearray()
    resp_bytes = response_bytes(SRAM_EXE_CMD_ID)

    cmd_bytes.append(SRAM_EXE_CMD_ID)
    cmd_bytes.extend(get_crc_bytes(cmd_bytes))

    uart_handle.write(cmd_bytes)

    uart_handle.reset_input_buffer()
    time.sleep(DELAY_50_MS)

    response_sram_exe = uart_handle.read(RESPONSE_LENGTH)

    if resp_bytes != response_sram_exe:
        logging.critical("Fail to transfer Second Loader execute command.")
        sys.exit(1)


def open_serial_port(port, baud):
    """Routine to open the comp port number handle.

    Args:
        port: Comport number
        baud: Given baud rate

    Returns:
        comport: UART port handle
    """
    comport = serial.Serial(port, baudrate=baud, stopbits=1, timeout=10)
    return comport


def main(args):
    """Crisis recovery utility routine for second loader into SRAM
    and program the ec image into the internal flash ."""
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument(
        "-p",
        "--uart_port",
        type=str,
        required=True,
        help="Servo EC UART port path to be specified",
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "-s",
        "--second_loader_file_path",
        type=str,
        help="second loader image file path",
    )
    group.add_argument(
        "-e",
        "--ec_file_path",
        type=str,
        help="EC image file path",
    )

    args = parser.parse_args()

    comp_port = os.path.normpath(args.uart_port)

    if args.second_loader_file_path:
        if not os.path.exists(args.second_loader_file_path):
            logging.error("Second Loader File not exist")
            sys.exit(1)

        uart_handle = open_serial_port(comp_port, 9600)
        process = command_write(
            uart_handle, HOST_NEGO_CMD_SEQ, HOST_NEGO_CMD_RESP_SEQ
        )
        if not process:
            logging.critical(
                "Timeout Error - Did not receive response from EC."
            )
            sys.exit(1)

        second_loader_image_transfer(uart_handle, args.second_loader_file_path)
        uart_handle.reset_input_buffer()
        req_ec_to_run_second_loader(uart_handle)

        uart_handle.close()

    elif args.ec_file_path:
        if not os.path.exists(args.ec_file_path):
            logging.error("EC File not exist")
            sys.exit(1)

        uart_handle = open_serial_port(comp_port, 57600)

        process = command_write(
            uart_handle, FLASH_PGM_CMD_SEQ, FLASH_PGM_CMD_RESP_SEQ
        )
        if not process:
            logging.critical(
                "Timeout Error - Did not receive response from EC."
            )
            sys.exit(1)

        ec_image_transfer(uart_handle, args.ec_file_path)

        uart_handle.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
