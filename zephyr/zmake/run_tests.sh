#!/bin/bash
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Run tests for zmake itself (not including Zephyr builds).

# Show commands being run.
set -x

# Exit if any command exits non-zero.
set -e

# cd to the directory containing this script.
cd "$(dirname "$(realpath -e "${BASH_SOURCE[0]}")")"

# Test the copy in-tree, instead of what setuptools or the ebuild
# installed.
export PYTHONPATH="${PWD}"

# Run pytest.
pytest . -v

# Check auto-generated README.md is as expected.
python -m zmake generate-readme --diff
