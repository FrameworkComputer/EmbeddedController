#!/bin/bash
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs all the unit tests in the util dir. Uses relative paths, so don't run
# from any ebuild.

# Show commands being run.
set -x

# Exit if any command exits non-zero.
set -e

# cd to the ec directory.
cd "$(dirname "$(realpath -e "${BASH_SOURCE[0]}")")"/..

# Run shell tests
cd util
./test-inject-keys.sh

# Run the Zephyr config tests
# NOTE: this uses the Zephyr version of kconfiglib, runs separately from
# test_kconfig_check.py
pytest check_zephyr_project_config_unittest.py

# Run the Zephyr check_compliance wrapper test.
# NOTE: these use vpython so they do not run correctly through pytest.
./zephyr_check_compliance_unittest.py
./kconfig_check_unittest.py
