#!/usr/bin/env python3
# Copyright 2018 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Program to convert sweetberry config to servod config template."""

import json
import os
import sys

from powerlog import Spower  # pylint:disable=import-error


def fetch_records(board_file):
    """Import records from servo_ina file.

    board files are json files, and have a list of tuples with
    the INA data.
    (name, rs, swetberry_num, net_name, channel)

    Args:
      board_file: board file

    Returns:
      list of tuples as described above.
    """
    data = None
    with open(board_file, encoding="utf-8") as input_file:
        data = json.load(input_file)
    return data


def write_to_file(file, sweetberry, inas):
    """Writes records of |sweetberry| to |file|
    Args:
      file: file to write to.
      sweetberry: sweetberry type. A or B.
      inas: list of inas read from board file.
    """

    with open(file, "w", encoding="utf-8") as pyfile:
        pyfile.write("inas = [\n")

        for rec in inas:
            if rec["sweetberry"] != sweetberry:
                continue

            # EX : ('sweetberry', 0x40, 'SB_FW_CAM_2P8', 5.0, 1.000, 3, False),
            channel, i2c_addr = Spower.CHMAP[rec["channel"]]
            record = (
                f"    ('sweetberry', 0x{i2c_addr:02x}, '{rec['name']}', 5.0, "
                f"{rec['rs']:f}, {channel:d}, 'True')"
                ",\n"
            )
            pyfile.write(record)

        pyfile.write("]\n")


def main(argv):
    """Entry function."""
    if len(argv) != 2:
        print("usage:")
        print(f" {argv[0]} input.board")
        return

    inputf = argv[1]
    basename = os.path.splitext(inputf)[0]

    inas = fetch_records(inputf)

    sweetberry = set(rec["sweetberry"] for rec in inas)

    if len(sweetberry) == 2:
        print(
            f"Converting {inputf} to {basename + '_a.py'} and "
            f"{basename + '_b.py'}"
        )
        write_to_file(basename + "_a.py", "A", inas)
        write_to_file(basename + "_b.py", "B", inas)
    else:
        print(f"Converting {inputf} to {basename + '.py'}")
        write_to_file(basename + ".py", sweetberry.pop(), inas)


if __name__ == "__main__":
    main(sys.argv)
