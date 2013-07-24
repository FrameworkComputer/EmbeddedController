#!/bin/bash -e
#
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Build all EC boards and run unit tests

# Build all boards except host
boards=$(ls -1 board | grep -v host)
for b in $boards; do
    echo ======== building $b
    make BOARD=$b
done

# Run unit tests
make BOARD=host runtests
