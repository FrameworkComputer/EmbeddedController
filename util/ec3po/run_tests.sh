#!/bin/bash
#
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

my_dir="$(realpath -e -- "$(dirname -- "$0")")"
parent_dir="$(realpath -e -- "$my_dir/..")"

PYTHONPATH="$parent_dir" python3 -s -m unittest \
    ec3po.console_unittest \
    ec3po.interpreter_unittest \
    && touch -- "$my_dir/.tests-passed"
