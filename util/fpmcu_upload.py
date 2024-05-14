#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Uploads an image on to an FPMCU dev board.

# Requirements

Install the Python dependencies in your chroot:

  (chroot) emerge pillow

Build the EC image with debug mode enabled:

  (chroot) platform/ec $ make BOARD=$BOARD EXTRA_CFLAGS=-DCONFIG_CMD_FPSENSOR_DEBUG

# Usage

Start servod with the board that is being tested:

  (chroot) sudo servod --board ${BOARD}

Flash the debug EC image onto FPMCU:

  (chroot) ./util/flash_ec --board=$BOARD --image=./build/$BOARD/ec.bin

Upload the image file:

  (chroot) ./util/fpmcu_upload.py -i /tmp/img1.png -b ${BOARD}

Run unit tests

  (chroot) python -m unittest util/fpmcu_upload.py
"""

import argparse
from dataclasses import dataclass
from enum import Enum
import io
import logging
import subprocess
import sys
from typing import List, Optional, Tuple
import unittest

# pylint: disable=import-error
from contextlib2 import ExitStack
from PIL import Image


# pylint:enable=import-error


class SensorByteOrder(Enum):
    """Storage order of bytes returned by the sensor"""

    ROW_MAJOR = 1
    COLUMN_MAJOR = 2


# TODO(b/267803007): Refactor this section of the code to remove duplication
# with run_device_tests.py
@dataclass
class BoardConfig:
    """Board-specific configuration."""

    name: str
    servo_uart_name: str
    sensor_height: int
    sensor_width: int
    sensor_byte_order: SensorByteOrder


BLOONCHIPPER_CONFIG = BoardConfig(
    name="bloonchipper",
    servo_uart_name="raw_fpmcu_console_uart_pty",
    sensor_width=160,
    sensor_height=160,
    sensor_byte_order=SensorByteOrder.ROW_MAJOR,
)

DARTMONKEY_CONFIG = BoardConfig(
    name="dartmonkey",
    servo_uart_name="raw_fpmcu_console_uart_pty",
    sensor_width=56,
    sensor_height=192,
    sensor_byte_order=SensorByteOrder.COLUMN_MAJOR,
)

BOARD_CONFIGS = {
    "bloonchipper": BLOONCHIPPER_CONFIG,
    "dartmonkey": DARTMONKEY_CONFIG,
}


def get_console(board_config: BoardConfig) -> Optional[str]:
    """Get the name of the console for a given board."""
    cmd = [
        "dut-control",
        board_config.servo_uart_name,
    ]
    logging.debug('Running command: "%s"', " ".join(cmd))

    with subprocess.Popen(cmd, stdout=subprocess.PIPE) as proc:
        for line in io.TextIOWrapper(proc.stdout):  # type: ignore[arg-type]
            logging.debug(line)
            pty = line.split(":")
            if len(pty) == 2 and pty[0] == board_config.servo_uart_name:
                return pty[1].strip()

    return None


def load_image_from_png(image_path: str) -> Image:
    """Load a PNG image from file"""
    if not image_path.endswith(".png"):
        raise ValueError(
            f"Input file must be png, sorry. Input was {image_path}"
        )

    return Image.open(image_path)


def pixel_value_to_hex(value: int) -> str:
    """Convert an 8-bit pixel value to a hex string of two characters"""
    if value < 0 or value > 255:
        raise ValueError(f"Pixel value {value} outside range 0-255")
    return f"{value:02x}"


def image_to_hex_str(image: Image, byte_order: SensorByteOrder) -> str:
    """Convert an 8-bit image to a string of hex characters"""
    image_str = ""
    num_cols, num_rows = image.size
    if byte_order == SensorByteOrder.ROW_MAJOR:
        for row in range(num_rows):
            for col in range(num_cols):
                image_str += pixel_value_to_hex(image.getpixel((col, row)))
    elif byte_order == SensorByteOrder.COLUMN_MAJOR:
        for col in range(num_cols):
            for row in range(num_rows):
                image_str += pixel_value_to_hex(image.getpixel((col, row)))

    return image_str


def split_hex_str(hex_str: str, chunk_size: int) -> List[Tuple[int, str]]:
    """Split a string with hex values into chunks with chunk_size values.

    hex_str is expected to contain hex values represented with two
    characters each.
    chunk_size is the number of hex values so the output string will contain
    2 x chunk_size characters.

    Returns a list of tuples (offset, hex_str).
    """

    if len(hex_str) % 2:
        raise ValueError("Input string does not represent hex values")

    chunks = []
    offset = 0
    while 2 * offset < len(hex_str):
        chunks.append((offset, hex_str[2 * offset : 2 * (offset + chunk_size)]))
        offset += chunk_size

    return chunks


def get_fpupload_cmds(board_config: BoardConfig, image: Image) -> List[str]:
    """Return the sequence of commands to upload the image on the FPMCU"""

    # Check that image has the correct size for the board selected.
    expected_size = (board_config.sensor_height, board_config.sensor_width)
    if image.size != expected_size:
        raise ValueError(
            f"Image size is {image.size} but expected {expected_size}"
        )

    logging.info("Image size: %s", image.size)

    # Convert the image as a string of hex values, then break it into chunks
    image_str = image_to_hex_str(image, board_config.sensor_byte_order)

    payload_size = 20
    cmds = []
    for offset, payload in split_hex_str(image_str, payload_size):
        cmds.append(f"fpupload {offset} {payload}\n")
    return cmds


def main():
    """Load a PNG image from file and transfer it to the FPMCU"""

    parser = argparse.ArgumentParser()

    default_board = "bloonchipper"
    parser.add_argument(
        "--board",
        "-b",
        help="Board (default: " + default_board + ")",
        default=default_board,
    )
    parser.add_argument(
        "--image",
        "-i",
        help="Image file in PNG",
        default="",
    )
    parser.add_argument(
        "--echo",
        help="Debug: whether or not to echo back the input image.",
        action="store_true",
    )

    args = parser.parse_args()
    board_config = BOARD_CONFIGS[args.board]

    image = load_image_from_png(args.image)
    fpupload_cmds = get_fpupload_cmds(board_config, image)

    with ExitStack() as stack:
        console = stack.enter_context(
            open(  # pylint:disable=consider-using-with
                get_console(board_config), "wb+", buffering=0
            )
        )
        for cmd in fpupload_cmds:
            console.write(cmd.encode())

        if args.echo:
            console.write("fpdownload".encode())


if __name__ == "__main__":
    sys.exit(main())


# Disable docstrings for unit tests
# pylint: disable=missing-module-docstring
# pylint: disable=missing-class-docstring
# pylint: disable=missing-function-docstring
class TestLoadImageFromPng(unittest.TestCase):
    def test_invalid_file_extension(self):
        with self.assertRaises(ValueError):
            load_image_from_png("not_a_png.jpg")

    def test_empty_file_path(self):
        with self.assertRaises(ValueError):
            load_image_from_png("")


class TestPixelValueToHex(unittest.TestCase):
    def test_value_below_zero(self):
        with self.assertRaises(ValueError):
            pixel_value_to_hex(-1)

    def test_value_above_255(self):
        with self.assertRaises(ValueError):
            pixel_value_to_hex(256)

    def test_valid_values(self):
        self.assertEqual(pixel_value_to_hex(0), "00")
        self.assertEqual(pixel_value_to_hex(1), "01")
        self.assertEqual(pixel_value_to_hex(10), "0a")
        self.assertEqual(pixel_value_to_hex(255), "ff")


class TestImageToHexStr(unittest.TestCase):
    # Use a small image with width = 5 and height = 3
    def test_row_major_order(self):
        image = Image.new("P", (5, 3))
        image.putpixel((0, 1), 255)
        image_str = image_to_hex_str(image, SensorByteOrder.ROW_MAJOR)
        self.assertEqual(len(image_str), 30)
        self.assertEqual(image_str[:2], "00")
        self.assertEqual(image_str[10:12], "ff")

    def test_col_major_order(self):
        image = Image.new("P", (5, 3))
        image.putpixel((0, 1), 255)
        image_str = image_to_hex_str(image, SensorByteOrder.COLUMN_MAJOR)
        self.assertEqual(len(image_str), 30)
        self.assertEqual(image_str[:2], "00")
        self.assertEqual(image_str[2:4], "ff")


class TestSplitHexStr(unittest.TestCase):
    def test_invalid_hex_str(self):
        with self.assertRaises(ValueError):
            split_hex_str("123", 4)

    def test_one_chunk(self):
        res = split_hex_str("12345678", 4)
        self.assertEqual(len(res), 1)
        self.assertEqual(res[0], (0, "12345678"))

    def test_one_partial_chunk(self):
        res = split_hex_str("123456", 4)
        self.assertEqual(len(res), 1)
        self.assertEqual(res[0], (0, "123456"))

    def test_two_chunks(self):
        res = split_hex_str("12345678", 2)
        self.assertEqual(len(res), 2)
        self.assertEqual(res[0], (0, "1234"))
        self.assertEqual(res[1], (2, "5678"))

    def test_two_partial_chunks(self):
        res = split_hex_str("12345678", 3)
        self.assertEqual(len(res), 2)
        self.assertEqual(res[0], (0, "123456"))
        self.assertEqual(res[1], (3, "78"))


class TestGetFpuploadCmds(unittest.TestCase):
    def test_invalid_size(self):
        image = Image.new("P", (10, 20))
        with self.assertRaises(ValueError):
            get_fpupload_cmds(DARTMONKEY_CONFIG, image)

    def test_valid_size(self):
        image = Image.new("P", (192, 56))
        cmds = get_fpupload_cmds(DARTMONKEY_CONFIG, image)
        self.assertEqual(len(cmds), 538)
        self.assertEqual(cmds[0].split()[0], "fpupload")
        self.assertEqual(cmds[0].split()[1], "0")
        self.assertEqual(cmds[1].split()[0], "fpupload")
        self.assertEqual(cmds[1].split()[1], "20")
        # The image has 10752 pixels which requires 538 commands for chunk size 20.
        self.assertEqual(cmds[537].split()[0], "fpupload")
        # 537 commands * offset 20 = 10740 final offset.
        self.assertEqual(cmds[537].split()[1], "10740")
