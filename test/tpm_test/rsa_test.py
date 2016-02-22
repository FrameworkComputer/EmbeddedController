#!/usr/bin/python
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for testing rsa functions using extended commands."""

import hashlib
import struct

import subcmd
import utils


_RSA_OPCODES = {
  'ENCRYPT': 0x00,
  'DECRYPT': 0x01,
  'SIGN': 0x02,
  'VERIFY': 0x03,
  'KEYGEN': 0x04
}


# TPM2 ALG codes.
_RSA_PADDING = {
  'PKCS1-SSA': 0x14,
  'PKCS1-ES': 0x15,
  'PKCS1-PSS': 0x16,
  'OAEP': 0x17
}


# TPM2 ALG codes.
_HASH = {
  'NONE': 0x00,
  'SHA1': 0x04,
  'SHA256': 0x0B
}


# Command format.
#
#   0x00 OP
#   0x00 PADDING
#   0x00 HASHING
#   0x00 MSB KEY LEN
#   0x00 LSB KEY LEN
#   0x00 MSB IN LEN
#   0x00 LSB IN LEN
#   .... IN
#   0x00 MSB DIGEST LEN
#   0x00 LSB DIGEST LEN
#   .... DIGEST
#
_RSA_CMD_FORMAT = '{o:c}{p:c}{h:c}{kl:s}{ml:s}{msg}{dl:s}{dig}'


def _decrypt_cmd(padding, hashing, key_len, msg):
  op = _RSA_OPCODES['DECRYPT']
  msg_len = len(msg)
  return _RSA_CMD_FORMAT.format(o=op, p=padding, h=hashing,
                                kl=struct.pack('>H', key_len),
                                ml=struct.pack('>H', msg_len), msg=msg,
                                dl='', dig='')


def _encrypt_cmd(padding, hashing, key_len, msg):
  op = _RSA_OPCODES['ENCRYPT']
  msg_len = len(msg)
  return _RSA_CMD_FORMAT.format(o=op, p=padding, h=hashing,
                                kl=struct.pack('>H', key_len),
                                ml=struct.pack('>H', msg_len), msg=msg,
                                dl='', dig='')


def _sign_cmd(padding, hashing, key_len, msg):
  op = _RSA_OPCODES['SIGN']
  digest = ''
  if hashing == _HASH['SHA1']:
      digest = hashlib.sha1(msg).digest()
  elif hashing == _HASH['SHA256']:
      digest = hashlib.sha256(msg).digest()
  digest_len = len(digest)
  return _RSA_CMD_FORMAT.format(o=op, p=padding, h=hashing,
                                kl=struct.pack('>H', key_len),
                                ml=struct.pack('>H', digest_len), msg=digest,
                                dl='', dig='')


def _verify_cmd(padding, hashing, key_len, sig, msg):
  op = _RSA_OPCODES['VERIFY']
  sig_len = len(sig)
  digest = ''
  if hashing == _HASH['SHA1']:
      digest = hashlib.sha1(msg).digest()
  elif hashing == _HASH['SHA256']:
      digest = hashlib.sha256(msg).digest()
  digest_len = len(digest)
  return _RSA_CMD_FORMAT.format(o=op, p=padding, h=hashing,
                                kl=struct.pack('>H', key_len),
                                ml=struct.pack('>H', sig_len), msg=sig,
                                dl=struct.pack('>H', digest_len), dig=digest)


#
# TEST VECTORS.
#
_ENCRYPT_INPUTS = (
  ('OAEP', 'SHA1', 768),
  ('OAEP', 'SHA256', 768),
  ('PKCS1-ES', 'NONE', 768),
  ('PKCS1-ES', 'NONE', 2048),
)


_SIGN_INPUTS = (
  ('PKCS1-SSA', 'SHA1', 768),
  ('PKCS1-SSA', 'SHA256', 768),
  ('PKCS1-PSS', 'SHA1', 768),
  ('PKCS1-PSS', 'SHA256', 768),
)


def _encrypt_tests(tpm):
  msg = 'Hello CR50!'

  for data in _ENCRYPT_INPUTS:
    padding, hashing, key_len = data
    test_name = 'RSA-ENC:%s:%s:%d' % data
    cmd = _encrypt_cmd(_RSA_PADDING[padding], _HASH[hashing], key_len, msg)
    wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.RSA, cmd))
    ciphertext = tpm.unwrap_ext_response(subcmd.RSA, wrapped_response)

    cmd = _decrypt_cmd(_RSA_PADDING[padding], _HASH[hashing],
                       key_len, ciphertext)
    wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.RSA, cmd))
    plaintext = tpm.unwrap_ext_response(subcmd.RSA, wrapped_response)
    if msg != plaintext:
      raise subcmd.TpmTestError('%s error:%s%s' % (
          test_name, utils.hex_dump(msg), utils.hex_dump(plaintext)))
    print('%sSUCCESS: %s' % (utils.cursor_back(), test_name))


def _sign_tests(tpm):
  msg = 'Hello CR50!'

  for data in _SIGN_INPUTS:
    padding, hashing, key_len = data
    test_name = 'RSA-SIGN:%s:%s:%d' % data
    cmd = _sign_cmd(_RSA_PADDING[padding], _HASH[hashing], key_len, msg)
    wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.RSA, cmd))
    signature = tpm.unwrap_ext_response(subcmd.RSA, wrapped_response)

    cmd = _verify_cmd(_RSA_PADDING[padding], _HASH[hashing],
                      key_len, signature, msg)
    wrapped_response = tpm.command(tpm.wrap_ext_command(subcmd.RSA, cmd))
    verified = tpm.unwrap_ext_response(subcmd.RSA, wrapped_response)
    expected = '\x01'
    if verified != expected:
      raise subcmd.TpmTestError('%s error:%s%s' % (
          test_name, utils.hex_dump(verified), utils.hex_dump(expected)))
    print('%sSUCCESS: %s' % (utils.cursor_back(), test_name))


def rsa_test(tpm):
  _encrypt_tests(tpm)
  _sign_tests(tpm)
