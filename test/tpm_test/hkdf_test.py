#!/usr/bin/env python2
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for testing HKDF using extended commands."""

from binascii import a2b_hex as a2b
from struct import pack

import subcmd
import utils


_HKDF_OPCODES = {
    'TEST_RFC': 0x00,
}


# Command format.
#
#    WIDTH         FIELD
#    1             OP
#    1             MSB SALT LEN
#    1             LSB SALT LEN
#    SALT_LEN      SALT
#    1             MSB IKM LEN
#    1             LSB IKM LEN
#    IKM_LEN       IKM
#    1             MSB INFO LEN
#    1             LSB INFO LEN
#    INFO_LEN      INFO
#    1             MSB OKM LEN
#    1             LSB OKM LEN
#
_HKDF_CMD_FORMAT = '{op:c}{sl:s}{salt}{ikml:s}{ikm}{infol:s}{info}{okml:s}'


def _rfc_test_cmd(salt, ikm, info, okml):
  op = _HKDF_OPCODES['TEST_RFC']
  return _HKDF_CMD_FORMAT.format(op=op,
                                 sl=pack('>H', len(salt)), salt=salt,
                                 ikml=pack('>H', len(ikm)), ikm=ikm,
                                 infol=pack('>H', len(info)), info=info,
                                 okml=pack('>H', okml))


#
# Test vectors for HKDF-SHA256 from RFC 5869.
#
_RFC_TEST_INPUTS = (
  (
    '0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b',
    '000102030405060708090a0b0c',
    'f0f1f2f3f4f5f6f7f8f9',
    ('3cb25f25faacd57a90434f64d0362f2a'
     '2d2d0a90cf1a5a4c5db02d56ecc4c5bf'
     '34007208d5b887185865'),
      'BASIC',
   ),
  (
    ('000102030405060708090a0b0c0d0e0f'
     '101112131415161718191a1b1c1d1e1f'
     '202122232425262728292a2b2c2d2e2f'
     '303132333435363738393a3b3c3d3e3f'
     '404142434445464748494a4b4c4d4e4f'),
    ('606162636465666768696a6b6c6d6e6f'
     '707172737475767778797a7b7c7d7e7f'
     '808182838485868788898a8b8c8d8e8f'
     '909192939495969798999a9b9c9d9e9f'
     'a0a1a2a3a4a5a6a7a8a9aaabacadaeaf'),
    ('b0b1b2b3b4b5b6b7b8b9babbbcbdbebf'
     'c0c1c2c3c4c5c6c7c8c9cacbcccdcecf'
     'd0d1d2d3d4d5d6d7d8d9dadbdcdddedf'
     'e0e1e2e3e4e5e6e7e8e9eaebecedeeef'
     'f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff'),
    ('b11e398dc80327a1c8e7f78c596a4934'
     '4f012eda2d4efad8a050cc4c19afa97c'
     '59045a99cac7827271cb41c65e590e09'
     'da3275600c2f09b8367793a9aca3db71'
     'cc30c58179ec3e87c14c01d5c1f3434f'
     '1d87'),
      'LONG INPUTS',
   ),
  (
    '0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b',
    '',
    '',
    ('8da4e775a563c18f715f802a063c5a31'
     'b8a11f5c5ee1879ec3454e5f3c738d2d'
     '9d201395faa4b61a96c8'),
      'ZERO SALT/INFO',
   )
)


def _rfc_tests(tpm):
  for data in _RFC_TEST_INPUTS:
    IKM, salt, info, OKM = map(a2b, data[:-1])
    test_name = 'HKDF:SHA256:%s' % data[-1]
    cmd = _rfc_test_cmd(salt, IKM, info, len(OKM))
    wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.HKDF, cmd))
    result = tpm.unwrap_ext_response(subcmd.HKDF, wrapped_response)

    if result != OKM:
      raise subcmd.TpmTestError('%s error:%s%s' % (
          test_name, utils.hex_dump(result), utils.hex_dump(OKM)))
    print('%sSUCCESS: %s' % (utils.cursor_back(), test_name))


def hkdf_test(tpm):
  _rfc_tests(tpm)
