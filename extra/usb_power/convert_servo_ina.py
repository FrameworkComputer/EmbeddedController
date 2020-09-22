#!/usr/bin/env python
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Program to convert power logging config from a servo_ina device
   to a sweetberry config.
"""

# Note: This is a py2/3 compatible file.

from __future__ import print_function
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
  if len(argv) != 2:
    print("usage:")
    print(" %s input.py" % argv[0])
    return

  inputf = argv[1]
  basename = os.path.splitext(inputf)[0]
  outputf = basename + '.board'
  outputs = basename + '.scenario'

  print("Converting %s to %s, %s" % (inputf, outputf, outputs))

  inas = fetch_records(basename)


  boardfile = open(outputf, 'w')
  scenario = open(outputs, 'w')

  boardfile.write('[\n')
  scenario.write('[\n')
  start = True

  for rec in inas:
    if start:
      start = False
    else:
      boardfile.write(',\n')
      scenario.write(',\n')

    record = '  {"name": "%s", "rs": %f, "sweetberry": "A", "channel": %d}' % (
             rec[2], rec[4], rec[1] - 64)
    boardfile.write(record)
    scenario.write('"%s"' % rec[2])

  boardfile.write('\n')
  boardfile.write(']')

  scenario.write('\n')
  scenario.write(']')

if __name__ == "__main__":
  main(sys.argv)
