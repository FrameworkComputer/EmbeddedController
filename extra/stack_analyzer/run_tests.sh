#!/bin/bash
#
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Discover all the unit tests in extra/stack_analyzer directory and run them.
python3 -m unittest discover -b -s extra/stack_analyzer -p "*_unittest.py"  \
    && touch extra/stack_analyzer/.tests-passed
