#!/usr/bin/env python2
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import print_function

import hashlib
import os
import struct

import subcmd
import utils


def upgrade(tpm):
  """Exercise the upgrade command.

  The target expect the upgrade extension command to have the following
  structure:

  cmd         1  value of FW_UPGRADE
  digest      4  first 4 bytes of sha1 of the remainder of the message
  block_base  4  address of the block to write
  data      var

  Args:
    tpm: a properly initialized tpmtest.TPM object
    Raises:
      subcmd.TpmTestError: In case of various test problems
  """
  cmd  = struct.pack('>I', 0)  # address
  cmd += struct.pack('>I', 0)  # data (a noop)
  wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.FW_UPGRADE, cmd))
  base_str = tpm.unwrap_ext_response(subcmd.FW_UPGRADE, wrapped_response)
  if len(base_str) < 4:
    raise subcmd.TpmTestError('Initialization error %d' %
                              ord(base_str[0]))
  base = struct.unpack_from('>4I', base_str)[3]
  if base == 0x44000:
    fname = 'build/cr50/RW/ec.RW_B.flat'
  elif base == 0x4000:
    fname = 'build/cr50/RW/ec.RW.flat'
  else:
    raise subcmd.TpmTestError('Unknown base address 0x%x' % base)
  fname = os.path.join(os.path.dirname(__file__), '../..', fname)
  data = open(fname, 'r').read()[:2000]
  transferred = 0
  block_size = 1024

  while transferred < len(data):
    tx_size = min(block_size, len(data) - transferred)
    chunk = data[transferred:transferred+tx_size]
    cmd  = struct.pack('>I', base)  # address
    h = hashlib.sha1()
    h.update(cmd)
    h.update(chunk)
    cmd = h.digest()[0:4] + cmd + chunk
    resp = tpm.unwrap_ext_response(subcmd.FW_UPGRADE,
                                   tpm.command(tpm.wrap_ext_command(
                                     subcmd.FW_UPGRADE, cmd)))
    code = ord(resp[0])
    if code:
      raise subcmd.TpmTestError('%x - resp %d' % (base, code))
    base += tx_size
    transferred += tx_size

  print('%sSUCCESS: Firmware upgrade' % (utils.cursor_back()))
