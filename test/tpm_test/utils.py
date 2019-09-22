#!/usr/bin/env python2
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Support functions for extended command based testing."""

import sys

if hasattr(sys.stdout, 'isatty') and sys.stdout.isatty():
  cursor_back_cmd = '\x1b[1D'  # Move one space to the left.
else:
  cursor_back_cmd = ''


def cursor_back():
  """Return a string which would move cursor one space left, if available.

  This is used to remove the remaining 'spinner' character after the test
  completes and its result is printed on the same line where the 'spinner' was
  spinning.

  """
  return cursor_back_cmd


def hex_dump(binstr):
  """Convert binary string into its multiline hex representation."""

  dump_lines = ['',]
  i = 0
  while i < len(binstr):
    strsize = min(16, len(binstr) - i)
    hexstr = ' '.join('%2.2x' % ord(x) for x in binstr[i:i+strsize])
    dump_lines.append(hexstr)
    i += strsize
  dump_lines.append('')
  return '\n'.join(dump_lines)
