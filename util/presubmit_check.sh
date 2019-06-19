#!/bin/bash
#
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ ! -e .tests-passed ]]; then
  echo 'Unit tests have not passed. Please run "make buildall -j".'
  exit 1
fi

# Directories that need to be tested by separate unit tests.
unittest_dirs="util/ec3po/ extra/stack_analyzer/"

changed=$(find ${PRESUBMIT_FILES} -newer .tests-passed)
# Filter out unittest_dirs files from changed files. They're handled separately.
for dir in $unittest_dirs; do
    changed=$(echo "${changed}" | grep -v "${dir}")
done
# Filter out flash_ec since it's not part of any unit tests.
changed=$(echo "${changed}" | grep -v util/flash_ec)
# Filter out uart_stress_tester.sh
changed=$(echo "${changed}" | grep -v util/uart_stress_tester.sh)
# Filter out this file itself.
changed=$(echo "${changed}" | grep -v util/presubmit_check.sh)
# Filter out the OWNERS file.
changed=$(echo "${changed}" | grep -v OWNERS)
if [[ -n "${changed}" ]]; then
  echo "Files have changed since last time unit tests passed:"
  echo "${changed}" | sed -e 's/^/  /'
  echo 'Please run "make buildall -j".'
  exit 1
fi

for dir in $unittest_dirs; do
    dir_files=$(echo "${PRESUBMIT_FILES}" | grep "${dir}")
    if [[ -z "${dir_files}" ]]; then
        continue
    fi

    if [[ ! -e "${dir}/.tests-passed" ]]; then
        echo "Unit tests have not passed.  Please run \"${dir}run_tests.sh\"."
        exit 1
    fi

    changed_files=$(find ${dir_files} -newer "${dir}/.tests-passed")
    if [[ -n "${changed_files}" ]] && [[ -n "${dir_files}" ]]; then
        echo "Files have changed since last time unit tests passed:"
        echo "${changed_files}" | sed -e 's/^/  /'
        echo "Please run \"${dir}run_tests.sh\"."
        exit 1
    fi
done
