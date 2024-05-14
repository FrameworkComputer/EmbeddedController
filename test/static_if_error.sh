#!/bin/bash -e
# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file is implemented similar to is_enabled_error.sh

TEST_DIR="$(dirname "${BASH_SOURCE[0]}")"
TEST_CMD="$(cat "${TEST_DIR}/RO/test/static_if_error.o.cmd")"
TEST_ERROR_COUNT=0
BAD_ERROR_MSG="This error should not be seen in the compiler output!"

fail() {
  echo "Fail"
  echo "$1"
  echo "${BUILD_OUTPUT}"
  TEST_ERROR_COUNT=$((TEST_ERROR_COUNT+1))
}

for test_macro in STATIC_IF STATIC_IF_NOT; do
  for test_value in 0 1 2 A "5 + 5"; do
    echo -n "Running TEST_MACRO=${test_macro} TEST_VALUE=${test_value}..."
    TEST_CMD_COMPLETE="
      ${TEST_CMD} \"-DTEST_MACRO=${test_macro}\" \"-DTEST_VALUE=${test_value}\""
    echo "${TEST_CMD_COMPLETE}"
    if BUILD_OUTPUT="$(sh -c "${TEST_CMD_COMPLETE}" 2>&1)"; then
      fail "Compilation should not have succeeded."
      continue
    fi

    if grep -q "${BAD_ERROR_MSG}" <<<"${BUILD_OUTPUT}"; then
      fail "TEST_MACRO was not defined."
      continue
    fi
  done
done

if [[ ${TEST_ERROR_COUNT} -eq 0 ]]; then
  echo "Pass!"
else
  echo "Fail! (${TEST_ERROR_COUNT} tests)"
fi
