#!/usr/bin/env python2
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for testing ecc functions using extended commands."""
import binascii
import hashlib
import os
import struct

import subcmd
import utils

_ECC_OPCODES = {
  'SIGN': 0x00,
  'VERIFY': 0x01,
  'KEYGEN': 0x02,
  'KEYDERIVE': 0x03,
}

_ECC_CURVES = {
  'NIST-P256': 0x03,
}

# TPM2 signature codes.
_SIGN_MODE = {
  'NONE': 0x00,
  'ECDSA': 0x18,
  # TODO(ngm): add support for SCHNORR.
  # 'SCHNORR': 0x1c
}

# TPM2 ALG codes.
_HASH = {
  'NONE': 0x00,
  'SHA1': 0x04,
  'SHA256': 0x0B
}

_HASH_FUNC = {
    'NIST-P256': hashlib.sha256
}

# Command format.
#
#   0x00 OP
#   0x00 CURVE_ID
#   0x00 SIGN_MODE
#   0x00 HASHING
#   0x00 MSB IN LEN
#   0x00 LSB IN LEN
#   .... IN
#   0x00 MSB DIGEST LEN
#   0x00 LSB DIGEST LEN
#   .... DIGEST
#
_ECC_CMD_FORMAT = '{o:c}{c:c}{s:c}{h:c}{ml:s}{msg}{dl:s}{dig}'


def _sign_cmd(curve_id, hash_func, sign_mode, msg):
  op = _ECC_OPCODES['SIGN']
  digest = hash_func(msg).digest()
  digest_len = len(digest)
  return _ECC_CMD_FORMAT.format(o=op, c=curve_id, s=sign_mode, h=_HASH['NONE'],
                               ml=struct.pack('>H', 0), msg='',
                               dl=struct.pack('>H', digest_len), dig=digest)


def _verify_cmd(curve_id, hash_func, sign_mode, msg, sig):
  op = _ECC_OPCODES['VERIFY']
  sig_len = len(sig)
  digest = hash_func(msg).digest()
  digest_len = len(digest)
  return _ECC_CMD_FORMAT.format(o=op, c=curve_id, s=sign_mode, h=_HASH['NONE'],
                               ml=struct.pack('>H', sig_len), msg=sig,
                               dl=struct.pack('>H', digest_len), dig=digest)


def _keygen_cmd(curve_id):
  op = _ECC_OPCODES['KEYGEN']
  return _ECC_CMD_FORMAT.format(o=op, c=curve_id, s=_SIGN_MODE['NONE'],
                               h=_HASH['NONE'], ml=struct.pack('>H', 0), msg='',
                               dl=struct.pack('>H', 0), dig='')


def _keyderive_cmd(curve_id, seed):
  op = _ECC_OPCODES['KEYDERIVE']
  seed_len = len(seed)
  return _ECC_CMD_FORMAT.format(o=op, c=curve_id, s=_SIGN_MODE['NONE'],
                               h=_HASH['NONE'], ml=struct.pack('>H', seed_len),
                               msg=seed, dl=struct.pack('>H', 0), dig='')


_SIGN_INPUTS = (
  ('NIST-P256', 'ECDSA'),
)


_KEYGEN_INPUTS = (
  ('NIST-P256',),
)


_KEYDERIVE_INPUTS = (
  # Curve-id, random seed size.
  ('NIST-P256', 32),
)


def _sign_test(tpm):
  msg = 'Hello CR50'

  for data in _SIGN_INPUTS:
    curve_id, sign_mode = data
    test_name = 'ECC-SIGN:%s:%s' % data
    cmd = _sign_cmd(_ECC_CURVES[curve_id], _HASH_FUNC[curve_id],
                    _SIGN_MODE[sign_mode], msg)
    wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.ECC, cmd))
    signature = tpm.unwrap_ext_response(subcmd.ECC, wrapped_response)

    cmd = _verify_cmd(_ECC_CURVES[curve_id], _HASH_FUNC[curve_id],
                      _SIGN_MODE[sign_mode], msg, signature)
    wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.ECC, cmd))
    verified = tpm.unwrap_ext_response(subcmd.ECC, wrapped_response)
    expected = '\x01'
    if verified != expected:
      raise subcmd.TpmTestError('%s error:%s:%s' % (
        test_name, utils.hex_dump(verified), utils.hex_dump(expected)))
    print('%sSUCCESS: %s' % (utils.cursor_back(), test_name))


def _keygen_test(tpm):
  for data in _KEYGEN_INPUTS:
    curve_id, = data
    test_name = 'ECC-KEYGEN:%s' % data
    cmd = _keygen_cmd(_ECC_CURVES[curve_id])
    wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.ECC, cmd))
    valid = tpm.unwrap_ext_response(subcmd.ECC, wrapped_response)
    expected = '\x01'
    if valid != expected:
      raise subcmd.TpmTestError('%s error:%s:%s' % (
        test_name, utils.hex_dump(valid), utils.hex_dump(expected)))
    print('%sSUCCESS: %s' % (utils.cursor_back(), test_name))


def _keyderive_test(tpm):
  for data in _KEYDERIVE_INPUTS:
    curve_id, seed_bytes = data
    seed = os.urandom(seed_bytes)
    test_name = 'ECC-KEYDERIVE:%s' % data[0]
    cmd = _keyderive_cmd(_ECC_CURVES[curve_id], seed)
    wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.ECC, cmd))
    valid = tpm.unwrap_ext_response(subcmd.ECC, wrapped_response)
    expected = '\x01'
    if valid != expected:
      raise subcmd.TpmTestError('%s error:%s:%s' % (
        test_name, utils.hex_dump(valid), utils.hex_dump(expected)))
    print('%sSUCCESS: %s' % (utils.cursor_back(), test_name))


def ecc_test(tpm):
  _sign_test(tpm)
  _keygen_test(tpm)
  _keyderive_test(tpm)
