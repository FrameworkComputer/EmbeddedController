#!/usr/bin/env python2
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for testing hash functions using extended commands."""

from __future__ import print_function

import hashlib
import struct

import subcmd
import utils

# Hash command modes
CMD_START = 0
CMD_CONT = 1
CMD_FINISH = 2
CMD_SINGLE = 3

# Hash modes
MODE_SHA1 = 0
MODE_SHA256 = 1

# A standard empty response to HASH extended commands.
EMPTY_RESPONSE = ''.join('%c' % x for x in (0x80, 0x01, 0x00, 0x00, 0x00, 0x0c,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x01))
test_inputs = (
  # SHA mode  cmd mode handle                text
  (MODE_SHA1, 'single', 0, 'anything really will work here'),
  (MODE_SHA256, 'single', 0, 'some more text, this time for sha256'),
  (MODE_SHA256, 'start',  1, 'some more text, this time for sha256'),
  (MODE_SHA256, 'cont',   1, 'some more text, this time for sha256'),
  (MODE_SHA256, 'start',  2, 'this could be anything, we just need to'),
  (MODE_SHA1, 'single',  3, 'interleave a SHA1 single calculation'),
  (MODE_SHA256, 'single',  3, 'interleave a SHA256 single calculation'),
  (MODE_SHA1, 'start',  3, 'let\'s interleave a sha1 calculation'),
  (MODE_SHA256, 'cont',   2, 'fill up a second context with something'),
  (MODE_SHA256, 'cont',   1, 'let\'s feed some more into context 1'),
  (MODE_SHA256, 'finish', 1, 'some more text, this time for sha256'),
  (MODE_SHA1, 'cont',   3, 'with two active sha256 calculations'),
  (MODE_SHA1, 'finish', 3, 'this should be enough'),
  (MODE_SHA256, 'finish', 2, 'it does not really matter what'),
)
def hash_test(tpm):
  """Exercise multiple hash threads simultaneously.

    Command structure, shared out of band with the test running on the target:

    field     |    size  |                  note
    ===================================================================
    hash_cmd  |    1     | 0 - start, 1 - cont., 2 - finish, 4 - single
    hash_mode |    1     | 0 - sha1, 1 - sha256
    handle    |    1     | session handle, ignored in 'single' mode
    text_len  |    2     | size of the text to process, big endian
    text      | text_len | text to hash

  Args:
    tpm: a tpm object used to communicate with the device

  Raises:
    subcmd.TpmTestError: on unexpected target responses
  """

  contexts = {}

  function_map = {
    MODE_SHA1: ('sha1', hashlib.sha1),
    MODE_SHA256: ('sha256', hashlib.sha256)
  }

  cmd_map = {
    'start': CMD_START,
    'cont': CMD_CONT,
    'finish': CMD_FINISH,
    'single': CMD_SINGLE
  }

  for test in test_inputs:
    hash_mode, cmd_name, handle, text = test

    mode_name, hash_func = function_map[hash_mode]
    hash_cmd = cmd_map[cmd_name]
    test_name = '%s:%s:%d' % (mode_name, cmd_name, handle)

    cmd = '%c' % hash_cmd
    cmd += '%c' % hash_mode
    cmd += '%c' % handle   # Ignored for single shots
    cmd += struct.pack('>H', len(text))
    cmd += text
    wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.HASH, cmd))
    if hash_cmd in (CMD_START, CMD_CONT):
      if hash_cmd == CMD_START:
        contexts[handle] = hash_func()
      h = contexts[handle]
      h.update(text)
      if wrapped_response != EMPTY_RESPONSE:
        raise subcmd.TpmTestError("Unexpected response to '%s': %s" %
                        (test_name, utils.hex_dump(wrapped_response)))
      continue
    if hash_cmd == CMD_FINISH:
      h = contexts[handle]
    elif hash_cmd == CMD_SINGLE:
      h = hash_func()
    else:
      raise subcmd.TpmTestError('Unknown command %d' % hash_cmd)
    h.update(text)
    digest = h.digest()
    result = wrapped_response[12:]
    if result != h.digest():
      raise subcmd.TpmTestError('%s error:%s%s' % (test_name,
                                         utils.hex_dump(digest),
                                         utils.hex_dump(result)))
    print('%sSUCCESS: %s' % (utils.cursor_back(), test_name))
