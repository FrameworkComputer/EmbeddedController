#!/bin/bash
#
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Runs command for N iterations or until commands fails.
# Usage:
#   ./run_until.sh <num_iterations> <command>
# Example:
#   ./run_until.sh 20  cros_sdk --working-dir . ./twister -c \
#       -T zephyr/test/pdc/ --no-upload-cros-rdb --test-only

MAX_ATTEMPTS=${1}
ATTEMPT_NUM=1
COMMAND=${*:2}

while [ "${ATTEMPT_NUM}" -le "${MAX_ATTEMPTS}" ]; do
  echo "Attempt ${ATTEMPT_NUM} / ${MAX_ATTEMPTS}"

  if ! ${COMMAND} ; then
    echo "Command failed on attempt ${ATTEMPT_NUM}."
    break
  fi
  ATTEMPT_NUM=$((ATTEMPT_NUM + 1))
done
