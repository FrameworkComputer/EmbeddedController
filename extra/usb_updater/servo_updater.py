#!/usr/bin/python
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import print_function

import argparse
import errno
import os
import subprocess
import time

import json
import fw_update
import ecusb.tiny_servo_common as c


def flash(brdfile, serialno, binfile):
  p = fw_update.Supdate()
  p.load_board(brdfile)
  p.connect_usb(serialname=serialno)
  p.load_file(binfile)

  # Start transfer and erase.
  p.start()
  # Upload the bin file
  print("Uploading %s" % binfile)
  p.write_file()

  # Finalize
  print("Done. Finalizing.")
  p.stop()


def select(vidpid, serialno, region, debuglog=False):
  if region not in ["rw", "ro"]:
    raise Exception("Region must be ro or rw")

  # Make sure device is up
  c.wait_for_usb(vidpid, serialname=serialno)

  # make a console
  pty = c.setup_tinyservod(vidpid, 0, serialname=serialno, debuglog=debuglog)

  cmd = "sysjump %s\nreboot" % region
  pty._issue_cmd(cmd)
  time.sleep(1)
  pty.close()


def main():
  parser = argparse.ArgumentParser(description="Image a servo micro device")
  parser.add_argument('-s', '--serialno', type=str,
      help="serial number to program", default=None)
  parser.add_argument('-b', '--board', type=str,
      help="Board configuration json file", default="servo_v4.json")
  parser.add_argument('-f', '--file', type=str,
      help="Complete ec.bin file", default="servo_v4.bin")
  parser.add_argument('-v', '--verbose', action="store_true",
      help="Chatty output")

  args = parser.parse_args()

  brdfile = args.board
  binfile = args.file
  serialno = args.serialno
  debuglog = (args.verbose is True)

  with open(brdfile) as data_file:
      data = json.load(data_file)

  vidpid = "%04x:%04x" % (int(data['vid'], 0), int(data['pid'], 0))

  select(vidpid, serialno, "ro", debuglog=debuglog)

  flash(brdfile, serialno, binfile)

  select(vidpid, serialno, "rw", debuglog=debuglog)

  flash(brdfile, serialno, binfile)

if __name__ == "__main__":
  main()

