#!/usr/bin/env python
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Sign EC firmware with 2048-bit RSA signature.

  Insert the RSA signature (256 bytes) at the end of the RW firmware
  and replace the public key constants with the new key in RO firmware.

  Example:
    ./util/sign_rsa [--rw] <pem> <ecfile>

    ./util/sign_rsa board/zinger/zinger_dev_key.pem build/zinger/ec.bin
"""
import logging
import sys

from subprocess import Popen, PIPE
from pem_extract_pubkey import extract_pubkey

# Size of a 2048-bit RSA signature
RSANUMBYTES = 256
# OpenSSL command to sign with SHA256andRSA
RSA_CMD = ["openssl", "dgst", "-sha256", "-sign"]

# Length reserved at the end of the RO partition for the public key
PUBKEY_RESERVED_SPACE = 528

def main():
  # Parse command line arguments
  if len(sys.argv) < 3:
    sys.stderr.write("Usage: %s [--rw] <pem> <ecfile>\n" % sys.argv[0])
    sys.exit(-1)
  if "--rw" in sys.argv:
    sys.argv.remove("--rw")
    has_ro = False
  else:
    has_ro = True
  pemfile = sys.argv[1]
  ecfile = sys.argv[2]

  # Get EC firmware content
  try:
    ec = file(ecfile).read()
  except:
    logging.error('cannot read firmware binary %s', ecfile)
    sys.exit(-1)

  # Extract the padded RW firmware to sign
  imglen = len(ec)/2
  rwdata = ec[imglen:-RSANUMBYTES] if has_ro else ec[:-RSANUMBYTES]
  # Compute the RSA signature using the OpenSSL binary
  RSA_CMD.append(pemfile)
  openssl = Popen(RSA_CMD, stdin=PIPE, stdout=PIPE)
  signature,_ = openssl.communicate(rwdata)

  if has_ro:
    # Get the public key values from the .pem file
    pubkey = extract_pubkey(pemfile, headerMode=False)
    # Add padding
    pubkey = pubkey + "\xff" * (PUBKEY_RESERVED_SPACE - len(pubkey))

  # Write back the signed EC firmware
  with open(ecfile, 'w') as fd:
    if has_ro:
      fd.write(ec[:imglen-len(pubkey)])
      fd.write(pubkey)
    fd.write(rwdata)
    fd.write(signature)

if __name__ == '__main__':
  try:
    main()
  except KeyboardInterrupt:
    sys.exit()
