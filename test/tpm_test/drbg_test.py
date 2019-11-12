#!/usr/bin/env python2
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module for testing hash functions using extended commands."""

from __future__ import print_function

from binascii import a2b_hex as a2b
from struct import pack

import subcmd
import utils


# A standard empty response to DRBG extended commands.
EMPTY_DRBG_RESPONSE = ''.join('%c' % x for x in (0x80, 0x01,
                                                 0x00, 0x00, 0x00, 0x0c,
                                                 0x00, 0x00, 0x00, 0x00,
                                                 0x00, subcmd.DRBG_TEST))

DRBG_INIT = 0
DRBG_RESEED = 1
DRBG_GENERATE = 2

test_inputs = (
  (DRBG_INIT,
   ('C40894D0C37712140924115BF8A3110C7258532365BB598F81B127A5E4CB8EB0',
    'FBB1EDAF92D0C2699F5C0A7418D308B09AC679FFBB0D8918C8E62D35091DD2B9',
    '2B18535D739F7E75AF4FF0C0C713DD4C9B0A6803D2E0DB2BDE3C4F3650ABF750')),
  (DRBG_RESEED,
   ('4D58A621857706450338CCA8A1AF5CD2BD9305F3475CF1A8752518DD8E8267B6',
    '0153A0A1D7487E2EE9915E2CAA8488F97239C67595F418D9503D0B11CC07044E', '')),
  (DRBG_GENERATE,
   ('39AE66C2939D1D73EF21AE22988B04CC7E8EA2D790C75E1FC6ACC7FEEEF90F98',
    '')),
  (DRBG_GENERATE,
   ('B8031829E07B09EEEADEBA149D0AC9F08B110197CD8BBDDC32744BCD66FCF3C4',
    'A1307377F6B472661BC3C6D44C035FB20A13CCB04D6601B2425FC4DDA3B6D7DF')),
  (DRBG_INIT,
   ('3A2D261884010CCB4C2C4D7B323CCB7BD4515089BEB749C565A7492710922164',
    '9E4D22471A4546F516099DD4D737967562D1BB77D774B67B7FE4ED893AE336CF',
    '5837CAA74345CC2D316555EF820E9F3B0FD454D8C5B7BDE68E4A176D52EE7D1C')),
  (DRBG_GENERATE,
   ('4D87985505D779F1AD98455E04199FE8F2FE8E550E6FEB1D26177A2C5B744B9F',
    '')),
  (DRBG_GENERATE,
   ('85D011A3B36AC6B25A792F213A1C22C80BFD1C5B47BCA04CD0D9834BB466447B',
    'B03863C42C9396B4936D83A551871A424C5A8EDBDC9D1E0E8E89710D58B5CA1E')),

)

_DRBG_INIT_FORMAT = '{op:c}{p0l:s}{p0}{p1l:s}{p1}{p2l:s}{p2}'
def _drbg_init_cmd(op, entropy, nonce, perso):
  return _DRBG_INIT_FORMAT.format(op=op,
                                  p0l=pack('>H', len(entropy)), p0=entropy,
                                  p1l=pack('>H', len(nonce)), p1=nonce,
                                  p2l=pack('>H', len(perso)), p2=perso)

_DRBG_GEN_FORMAT = '{op:c}{p0l:s}{p0}{p1l:s}'

def _drbg_gen_cmd(inp, out):
  outlen = len(out)
  if outlen == 0:
    outlen = 32 # if we don't care about output value, still need to have it
  return _DRBG_GEN_FORMAT.format(op=DRBG_GENERATE,
                                 p0l=pack('>H', len(inp)), p0=inp,
                                 p1l=pack('>H', outlen))


def drbg_test(tpm):
  """Runs DRBG test case.

  Args:
    tpm: a tpm object used to communicate with the device

  Raises:
    subcmd.TpmTestError: on unexpected target responses
  """

  for test in test_inputs:
    drbg_op, drbg_params = test
    if drbg_op == DRBG_INIT:
      entropy, nonce, perso = drbg_params
      cmd = _drbg_init_cmd(drbg_op, a2b(entropy), a2b(nonce), a2b(perso))
      response = tpm.command(tpm.wrap_ext_command(subcmd.DRBG_TEST, cmd))
      if response != EMPTY_DRBG_RESPONSE:
        raise subcmd.TpmTestError("Unexpected response to DRBG_INIT: %s" %
                        (utils.hex_dump(wrapped_response)))
    elif drbg_op == DRBG_RESEED:
      entropy, inp1, inp2 = drbg_params
      cmd = _drbg_init_cmd(drbg_op, a2b(entropy), a2b(inp1), a2b(inp2))
      response = tpm.command(tpm.wrap_ext_command(subcmd.DRBG_TEST, cmd))
      if response != EMPTY_DRBG_RESPONSE:
        raise subcmd.TpmTestError("Unexpected response to DRBG_RESEED: %s" %
                        (utils.hex_dump(wrapped_response)))
    elif drbg_op == DRBG_GENERATE:
      inp, expected = drbg_params
      cmd = _drbg_gen_cmd(a2b(inp), a2b(expected))
      response = tpm.command(tpm.wrap_ext_command(subcmd.DRBG_TEST, cmd))
      if expected != '':
        result = response[12:]
        if a2b(expected) != result:
           raise subcmd.TpmTestError('error:\nexpected %s\nreceived %s' %
                                     (utils.hex_dump(a2b(expected)),
                                      utils.hex_dump(result)))
  print('%sSUCCESS: %s' % (utils.cursor_back(), 'DRBG test'))
