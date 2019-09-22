#!/usr/bin/env python2
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Subcommand codes that specify the crypto module."""

# Keep these codes in sync with include/extension.h.
AES = 0
HASH = 1
RSA = 2
ECC = 3
FW_UPGRADE = 4
HKDF = 5
ECIES = 6

# The same exception class used by all tpmtest modules.
class TpmTestError(Exception):
  pass
