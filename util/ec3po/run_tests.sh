#!/bin/bash
#
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Discover all the unit tests in the ec3po directory and run them.
python3 -m unittest discover -b -s util/ec3po/ -p "*_unittest.py"  \
    && touch util/ec3po/.tests-passed
