#!/usr/bin/env python3
# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Program to convert power logging config from a servo_ina device
   to a sweetberry config.
"""

import os
import sys


def fetch_records(basename):
    """Import records from servo_ina file.

    servo_ina files are python imports, and have a list of tuples with
    the INA data.
    (inatype, i2caddr, rail name, bus voltage, shunt ohms, mux, True)

    Args:
      basename: python import name (filename -.py)

    Returns:
      list of tuples as described above.
    """
    ina_desc = __import__(basename)
    return ina_desc.inas


def main(argv):
    """Main function."""
    if len(argv) != 2:
        print("usage:")
        print(f" {argv[0]} input.py")
        return

    inputf = argv[1]
    basename = os.path.splitext(inputf)[0]
    outputf = basename + ".board"
    outputs = basename + ".scenario"

    print(f"Converting {inputf} to {outputf}, {outputs}")

    inas = fetch_records(basename)

    with open(outputf, "w", encoding="utf-8") as boardfile, open(
        outputs, "w", encoding="utf-8"
    ) as scenario:
        boardfile.write("[\n")
        scenario.write("[\n")
        start = True

        for rec in inas:
            if start:
                start = False
            else:
                boardfile.write(",\n")
                scenario.write(",\n")

            record = (
                f'  {{"name": "{rec[2]}", "rs": {rec[4]:f}, '
                f'"sweetberry": "A", "channel": {rec[1] - 64:d}}}'
            )
            boardfile.write(record)
            scenario.write(f'"{rec[2]}"')

        boardfile.write("\n")
        boardfile.write("]")

        scenario.write("\n")
        scenario.write("]")


if __name__ == "__main__":
    main(sys.argv)
