#!/usr/bin/env python3
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Takes a lcov info file as input and normalizes symlinks from SF: lines."""

import fileinput
import os
import sys

for line in fileinput.input():
    if line.startswith('SF:'):
        path = line[3:].rstrip()
        sys.stdout.write('SF:%s\n' % os.path.realpath(path))
    else:
        sys.stdout.write(line)
