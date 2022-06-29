#!/bin/bash

# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set +x

zmake build --clobber test-drivers || exit 1

echo "Searching for '${1}'..."
found_errors=0
loop_count=100
EXECUTABLE=./build/zephyr/test-drivers/build-singleimage/zephyr/zephyr.exe
while [ "${loop_count}" -gt 0 ]; do
  seed=${RANDOM}
  echo "[$((100 - loop_count))] Using seed=${seed}"
  error_count=$(timeout 150s "${EXECUTABLE}" -seed="${seed}" 2>&1 |
    grep -c "${1}")
  status=$?

  if [ "${status}" -eq 124 ]; then
    echo "    Timeout"
  elif [ "${status}" -ne 0 ]; then
    echo "    Error/timeout"
  fi
  if [ "${error_count}" -gt 0 ]; then
    echo "    Found ${error_count} errors matching '${1}'"
  fi

  found_errors=$((found_errors + error_count))
  loop_count=$((loop_count - 1))
done

if [ "${found_errors}" -ne 0 ]; then
  exit 1
fi
