#!/usr/bin/env python2
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for testing hash functions using extended commands."""

from __future__ import print_function

import hashlib
import hmac
import struct

import subcmd
import utils

# Hash command modes
CMD_HASH_START = 0
CMD_HASH_CONT = 1
CMD_HASH_FINISH = 2
CMD_HASH = 3
CMD_HMAC_SW = 4
CMD_HMAC_HW = 5


# Hash algorithm
ALG_SHA1 = 0
ALG_SHA256 = 1
ALG_SHA384 = 2
ALG_SHA512 = 3

# A standard empty response to HASH extended commands.
EMPTY_RESPONSE = ''.join('%c' % x for x in (0x80, 0x01, 0x00, 0x00, 0x00, 0x0c,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x01))
test_inputs = (
  # Hash cmd    alg      handle  hmac_key           text
  (CMD_HMAC_SW, ALG_SHA256, 0, 'hmac_key1', 'some text, this time for sw hmac'),
  (CMD_HMAC_SW, ALG_SHA1,   0, 'hmac_key2', 'some text, this time for sw hmac'),
  (CMD_HMAC_SW, ALG_SHA384, 0, 'hmac_key3', 'some text, this time for sw hmac'),
  (CMD_HMAC_SW, ALG_SHA512, 0, 'hmac_key4', 'some text, this time for sw hmac'),
  (CMD_HMAC_HW, ALG_SHA256, 0, 'hmac_key5', 'some text, this time for hw hmac'),
  (CMD_HMAC_SW, ALG_SHA256, 0, 'very long hmac_key 456456789012345', '   text'),
  (CMD_HMAC_HW, ALG_SHA256, 0, 'very long hmac_key 123456789012345', '   text'),
  (CMD_HMAC_SW, ALG_SHA384, 0, 'very long hmac_key 456456789012345', '   text'),
  (CMD_HMAC_SW, ALG_SHA512, 0, 'very long hmac_key 456456789012345', '   text'),
  (CMD_HASH,        ALG_SHA1,   0, '', ''),
  (CMD_HASH,        ALG_SHA256, 0, '', ''),
  (CMD_HASH,        ALG_SHA1,   0, '', 'anything really will work here'),
  (CMD_HASH,        ALG_SHA256, 0, '', 'some more text, this time for sha256'),
  (CMD_HASH_START,  ALG_SHA256, 1, '', 'some more text, this time for sha256'),
  (CMD_HASH_CONT,   ALG_SHA256, 1, '', 'some more text, this time for sha256'),
  (CMD_HASH_START,  ALG_SHA256, 2, '', 'this could be anything here'),
  (CMD_HASH,        ALG_SHA1,   3, '', 'interleave a SHA1 single shot'),
  (CMD_HASH,        ALG_SHA256, 3, '', 'interleave a SHA256 single shot'),
  (CMD_HASH_START,  ALG_SHA1,   3, '', 'let\'s interleave a sha1 calculation'),
  (CMD_HASH_CONT,   ALG_SHA256, 2, '', 'fill up a second context with data'),
  (CMD_HASH_CONT,   ALG_SHA256, 1, '', 'let\'s feed some more into context 1'),
  (CMD_HASH_FINISH, ALG_SHA256, 1, '', 'some more text, this time for sha256'),
  (CMD_HASH_CONT,   ALG_SHA1,   3, '', 'with two active sha256 calculations'),
  (CMD_HASH_FINISH, ALG_SHA1,   3, '', 'this should be enough'),
  (CMD_HASH_FINISH, ALG_SHA256, 2, '', 'it does not really matter what'),
  (CMD_HASH,        ALG_SHA384, 0, '', 'some more text, this time for sha384'),
  (CMD_HASH,        ALG_SHA512, 0, '', 'some more text, this time for sha512'),
  (CMD_HASH_START,  ALG_SHA256, 0, '', 'some more text, this time for sha256'),
  (CMD_HASH_START,  ALG_SHA384, 1, '', 'some more text, this time for sha384'),
  (CMD_HASH_CONT,   ALG_SHA384, 1, '', 'some more text, this time for sha384'),
  (CMD_HASH_CONT,   ALG_SHA256, 0, '', 'some more text, this time for sha256'),
  (CMD_HASH_START,  ALG_SHA512, 2, '', 'some more text, this time for sha512'),
  (CMD_HASH_CONT,   ALG_SHA512, 2, '', 'some more text, this time for sha512'),
  (CMD_HASH_FINISH, ALG_SHA512, 2, '', 'this should be enough'),
  (CMD_HASH_FINISH, ALG_SHA256, 0, '', 'this should be enough'),
  (CMD_HASH_FINISH, ALG_SHA384, 1, '', 'this should be enough'),
)

def hash_test(tpm):
  """Exercise multiple hash threads simultaneously.

    Command structure, shared out of band with the test running on the target:

    field     |    size  |                  note
    ===================================================================
    hash_cmd  |    1     | 0 - start, 1 - cont., 2 - finish, 3 - single
              |          | 4 - SW HMAC single shot (TPM code)
              |          | 5 - HW HMAC SHA256 single shot (dcrypto code)
    hash_alg  |    1     | 0 - sha1, 1 - sha256, 2 - sha384, 3 - sha512
    handle    |    1     | session handle, ignored in 'single' mode
    text_len  |    2     | size of the text to process, big endian
    text      | text_len | text to hash
            for HMAC single shot only:
    key_len   |    2     | size of the key for HMAC, big endian
    key       | key_len  | key for HMAC single shot
  Args:
    tpm: a tpm object used to communicate with the device

  Raises:
    subcmd.TpmTestError: on unexpected target responses
  """

  contexts = {}

  alg_map = {
    ALG_SHA1: ('sha1', hashlib.sha1),
    ALG_SHA256: ('sha256', hashlib.sha256),
    ALG_SHA384: ('sha384', hashlib.sha384),
    ALG_SHA512: ('sha512', hashlib.sha512),
  }

  cmd_map = {
    CMD_HASH_START: 'hash start',
    CMD_HASH_CONT: 'hash cont',
    CMD_HASH_FINISH: 'hash finish',
    CMD_HASH: 'hash',
    CMD_HMAC_SW: 'hmac sw',
    CMD_HMAC_HW: 'hmac hw'
  }

  for test in test_inputs:
    hash_cmd, hash_alg, handle, hmac_key, text = test
    mode_name = cmd_map[hash_cmd]
    alg_name, hash_func = alg_map[hash_alg]

    test_name = '%s:%s:%d' % (mode_name, alg_name, handle)

    cmd = '%c' % hash_cmd
    cmd += '%c' % hash_alg
    cmd += '%c' % handle   # Ignored for single shots

    cmd += struct.pack('>H', len(text))
    cmd += text
    # for HMAC add key
    if hash_cmd in (CMD_HMAC_SW, CMD_HMAC_HW):
      cmd += struct.pack('>H', len(hmac_key))
      cmd += hmac_key
    wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.HASH, cmd))
    if hash_cmd in (CMD_HASH_START, CMD_HASH_CONT):
      if hash_cmd == CMD_HASH_START:
        contexts[handle] = hash_func()
      h = contexts[handle]
      h.update(text)
      if wrapped_response != EMPTY_RESPONSE:
        raise subcmd.TpmTestError("Unexpected response to '%s': %s" %
                        (test_name, utils.hex_dump(wrapped_response)))
      continue
    if hash_cmd == CMD_HASH_FINISH:
      h = contexts[handle]
    elif hash_cmd == CMD_HASH:
      h = hash_func()
    elif hash_cmd in (CMD_HMAC_SW, CMD_HMAC_HW):
      h = hmac.new(bytes(hmac_key), digestmod=hash_func)
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
