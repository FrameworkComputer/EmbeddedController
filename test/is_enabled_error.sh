#!/bin/bash -e
# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

TEST_DIR="$(dirname "${BASH_SOURCE[0]}")"

TEST_CMD="$(cat "${TEST_DIR}/RO/test/is_enabled_error.o.cmd")"

TEST_ERROR_COUNT=0

for test_value in 0 1 2 A "5 + 5"; do
	echo -n "Running TEST_VALUE=${test_value}..."
	TEST_CMD_COMPLETE="${TEST_CMD} \"-DTEST_VALUE=${test_value}\""
	if BUILD_OUTPUT="$(sh -c "${TEST_CMD_COMPLETE}" 2>&1)"; then
		echo "Fail"
		echo "Compilation should not have succeeded for" \
		     "TEST_VALUE=${test_value}"
		echo "${BUILD_OUTPUT}"
		TEST_ERROR_COUNT=$((TEST_ERROR_COUNT+1))
		continue
	fi

	EXPECTED_ERROR="CONFIG_VALUE must be <blank>, or not defined"
	if grep -q "${EXPECTED_ERROR}" <<< "${BUILD_OUTPUT}"; then
		echo "OK"
	else
		echo "Fail"
		echo "Expected to find: ${EXPECTED_ERROR}"
		echo "Actual error:"
		echo "${BUILD_OUTPUT}"
		TEST_ERROR_COUNT=$((TEST_ERROR_COUNT+1))
	fi
done

if [[ ${TEST_ERROR_COUNT} -eq 0 ]]; then
	echo "Pass!"
else
	echo "Fail! (${TEST_ERROR_COUNT} tests)"
fi
