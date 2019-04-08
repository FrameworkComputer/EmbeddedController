#!/bin/bash
#
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

ec_commands_file_in="include/ec_commands.h"
ec_commands_file_out="build/kernel/include/linux/mfd/cros_ec_commands.h"

# Check if ec_commands.h has changed.
echo ${PRESUBMIT_FILES} | grep -q "${ec_commands_file_in}" || exit 0

if [ ! -f "${ec_commands_file_out}" ]; then
  echo "A new cros_ec_commands.h must be generated."
  echo 'Please run "make buildall" or "make build_cros_ec_commands"'.
  exit 1
fi

if [ "${ec_commands_file_out}" -ot "${ec_commands_file_in}" ]; then
  echo "cros_ec_commands.h is out of date."
  echo 'Please run "make buildall" or "make build_cros_ec_commands"'.
  exit 1
fi
