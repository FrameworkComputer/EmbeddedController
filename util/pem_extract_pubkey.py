#!/usr/bin/env python
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Extract the public key from a .pem RSA private key file (PKCS#1),
  and compute the public key coefficients used by the signature verification
  code.

  Example:
    ./util/pem_extract_pubkey board/zinger/zinger_dev_key.pem

  Note: to generate a suitable private key :
  RSA 2048-bit with public exponent F4 (65537)
  you can use the following OpenSSL command :
  openssl genrsa -F4 -out private.pem 2048
"""

import array
import base64
import sys

VERSION = '0.0.1'

"""
RSA Private Key file (PKCS#1) encoding :

It starts and ends with the tags:
-----BEGIN RSA PRIVATE KEY-----
BASE64 ENCODED DATA
-----END RSA PRIVATE KEY-----

The base64 encoded data is using an ASN.1 / DER structure :

RSAPrivateKey ::= SEQUENCE {
  version           Version,
  modulus           INTEGER,  -- n
  publicExponent    INTEGER,  -- e
  privateExponent   INTEGER,  -- d
  prime1            INTEGER,  -- p
  prime2            INTEGER,  -- q
  exponent1         INTEGER,  -- d mod (p-1)
  exponent2         INTEGER,  -- d mod (q-1)
  coefficient       INTEGER,  -- (inverse of q) mod p
  otherPrimeInfos   OtherPrimeInfos OPTIONAL
}
"""

PEM_HEADER='-----BEGIN RSA PRIVATE KEY-----'
PEM_FOOTER='-----END RSA PRIVATE KEY-----'

class PEMError(Exception):
  """Exception class for pem_extract_pubkey utility."""

# "Constructed" bit in DER tag
DER_C=0x20
# DER Sequence tag (always constructed)
DER_SEQUENCE=DER_C|0x10
# DER Integer tag
DER_INTEGER=0x02

class DER:
  """DER encoded binary data storage and parser."""
  def __init__(self, data):
    # DER encoded binary data
    self._data = data
    # Initialize index in the data stream
    self._idx = 0

  def get_byte(self):
    octet = ord(self._data[self._idx])
    self._idx += 1
    return octet

  def get_len(self):
    octet = self.get_byte()
    if octet == 0x80:
      raise PEMError('length indefinite form not supported')
    if octet & 0x80: # Length long form
      bytecnt = octet & ~0x80
      total = 0
      for i in range(bytecnt):
        total = (total << 8) | self.get_byte()
      return total
    else: # Length short form
      return octet

  def get_tag(self):
    tag = self.get_byte()
    length = self.get_len()
    data = self._data[self._idx:self._idx + length]
    self._idx += length
    return {"tag" : tag, "length" : length, "data" : data}

def pem_get_mod(filename):
  """Extract the modulus from a PEM private key file.

  the PEM file is DER encoded according the structure quoted above.

  Args:
    filename : Full path to the .pem private key file.

  Raises:
    PEMError: If unable to parse .pem file or invalid file format.
  """
  # Read all the content of the .pem file
  content = file(filename).readlines()
  # Check the PEM RSA Private key tags
  if content[0].strip() != PEM_HEADER:
    raise PEMError('invalid PEM private key header')
  if content[-1].strip() != PEM_FOOTER:
    raise PEMError('invalid PEM private key footer')
  # Decode the DER binary stream from the base64 data
  b64 = "".join([l.strip() for l in content[1:-1]])
  der = DER(base64.b64decode(b64))

  # Parse the DER and fail at the first error
  # The private key should be a (constructed) sequence
  seq = der.get_tag()
  if seq["tag"] != DER_SEQUENCE:
    raise PEMError('expecting an ASN.1 sequence')
  seq = DER(seq["data"])

  # 1st field is Version
  ver = seq.get_tag()
  if ver["tag"] != DER_INTEGER:
    raise PEMError('version field should be an integer')

  # 2nd field is Modulus
  mod = seq.get_tag()
  if mod["tag"] != DER_INTEGER:
    raise PEMError('modulus field should be an integer')
  # 2048 bits + mandatory ASN.1 sign (0) => 257 Bytes
  if mod["length"] != 257 or mod["data"][0] != '\x00':
    raise PEMError('Invalid key length (expecting 2048 bits)')

  # 3rd field is Public Exponent
  exp = seq.get_tag()
  if exp["tag"] != DER_INTEGER:
    raise PEMError('exponent field should be an integer')
  if exp["length"] != 3 or exp["data"] != "\x01\x00\x01":
    raise PEMError('the public exponent must be F4 (65537)')

  return mod["data"]

def modinv(a, m):
  """ The multiplicitive inverse of a in the integers modulo m.

  Return b when a * b == 1 mod m
  """
  # Extended GCD
  lastrem, rem = abs(a), abs(m)
  x, lastx, y, lasty = 0, 1, 1, 0
  while rem:
    lastrem, (quotient, rem) = rem, divmod(lastrem, rem)
    x, lastx = lastx - quotient*x, x
    y, lasty = lasty - quotient*y, y
  #
  if lastrem != 1:
    raise ValueError
  x = lastx * (-1 if a < 0 else 1)
  return x % m

def to_words(n, count):
  h = '%x' % n
  s = ('0'*(len(h) % 2) + h).zfill(count*8).decode('hex')
  return array.array("I", s[::-1])

def compute_mod_parameters(modulus):
  ''' Prepare/pre-compute coefficients for the RSA public key signature
    verification code.
  '''
  # create an array of uint32_t to store the modulus but skip the sign byte
  w = array.array("I",modulus[1:])
  # all integers in DER encoded .pem file are big endian.
  w.reverse()
  w.byteswap()
  # convert the big-endian modulus to a big integer for the computations
  N = 0
  for i in range(len(modulus)):
    N = (N << 8) | ord(modulus[i])
  # -1 / N[0] mod 2^32
  B = 0x100000000L
  n0inv = B - modinv(w[0], B)
  # R = 2^(modulo size); RR = (R * R) % N
  RR = pow(2, 4096, N)
  rr_words = to_words(RR, 64)

  return {'mod':w, 'rr':rr_words, 'n0inv':n0inv}

def print_header(params):
  print "{\n\t.n = {%s}," % (",".join(["0x%08x" % (i) for i in params['mod']]))
  print "\t.rr = {%s}," % (",".join(["0x%08x" % (i) for i in params['rr']]))
  print "\t.n0inv = 0x%08x\n};" % (params['n0inv'])

def dump_blob(params):
  mod_bin = params['mod'].tostring()
  rr_bin = params['rr'].tostring()
  n0inv_bin = array.array("I",[params['n0inv']]).tostring()
  return mod_bin + rr_bin + n0inv_bin

def extract_pubkey(pemfile, headerMode=True):
  # Read the modulus in the .pem file
  mod = pem_get_mod(sys.argv[1])
  # Pre-compute the parameters used by the verification code
  p = compute_mod_parameters(mod)

  if headerMode:
    # Generate a C header file with the parameters
    print_header(p)
  else:
    # Generate the packed structure as a binary blob
    return dump_blob(p)

if __name__ == '__main__':
  try:
    if len(sys.argv) < 2:
      raise PEMError('Invalid arguments. Usage: ./pem_extract_pubkey priv.pem')
    extract_pubkey(sys.argv[1])
  except KeyboardInterrupt:
    sys.exit(0)
  except PEMError as e:
    sys.stderr.write("Error: %s\n" % (e.message))
    sys.exit(1)
