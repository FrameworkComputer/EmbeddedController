#!/bin/bash
#
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Verify there is no CPRINTS("....\n", ...) statements added to the code.
upstream_branch="$(git rev-parse --abbrev-ref --symbolic-full-name @{u} \
    2>/dev/null)"
if [[ -z ${upstream_branch} ]]; then
  echo "Current branch does not have an upstream branch" >&2
  exit 1
fi
# This will print the offending CPRINTS invocations, if any, and the names of
# the files they are in.
if git diff "${upstream_branch}" HEAD | grep -e '^+\(.*CPRINTS(.*\\n"\|++\)' |
    grep CPRINTS -B1 >&2 ; then
  echo "error: CPRINTS strings should not include newline characters" >&2
  exit 1
fi

# Directories that need to be tested by separate unit tests.
unittest_dirs="util/ec3po/ extra/stack_analyzer/"

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
