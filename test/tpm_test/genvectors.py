#!/usr/bin/env python2
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for generating AES test vectors."""

from binascii import b2a_hex as b2a
from Crypto.Cipher import AES
from itertools import izip_longest
import os

modes = {
    AES.MODE_CBC: 'CBC',
    AES.MODE_CFB: 'CFB',
    AES.MODE_OFB: 'OFB',
}

template = \
'''
  <crypto_test name="AES:{mode}{key_bits} {test_num}">
    <clear_text format="hex">
      {pt}
    </clear_text>
    <key>
      {key}
    </key>
    <cipher_text>
      {ct}
    </cipher_text>
    <iv>
      {iv}
    </iv>
  </crypto_test>
'''

def h2be(v):
    # Convert input big-endian byte-string to 4-byte segmented
    # little-endian words.  Pad-bytes (if necessary) are the empty string.
    word = [iter(v)] * 4
    return ''.join([
        ''.join(b[::-1]) for b in izip_longest(*word, fillvalue='')
    ])


for mode in [AES.MODE_CBC, AES.MODE_CFB, AES.MODE_OFB]:
    for key_bytes in [16, 24, 32]:
        test_num = 0
        for pt_len in [5, 16, 21, 32]:
            # CBC mode requires block sized inputs.
            if mode == AES.MODE_CBC and pt_len % 16:
                continue
            test_num += 1

            actual_pt_len = pt_len
            if pt_len % 16:
                pt_len = 16 * ((pt_len / 16) + 1)

            key = os.urandom(key_bytes)
            iv = os.urandom(16)
            pt = os.urandom(pt_len)

            obj = AES.new(key, mode=mode, IV=iv, segment_size=128)
            ct = obj.encrypt(pt)
            obj = AES.new(key, mode=mode, IV=iv, segment_size=128)

            assert obj.decrypt(ct)[:pt_len] == pt

            print template.format(mode=modes[mode],
                                  key_bits=str(key_bytes * 8),
                                  test_num=str(test_num),
                                  pt=b2a(h2be(pt[:actual_pt_len])),
                                  key=b2a(h2be(key)),
                                  ct=b2a(h2be(ct[:actual_pt_len])),
                                  iv=b2a(h2be(iv))),


