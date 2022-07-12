#!/bin/bash
# Copyright 2022 The ChromiumOS Authors.
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

# Run pytest
pytest util "$@"
