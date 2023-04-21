#!/bin/bash
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Run tests for zmake itself (not including Zephyr builds).
# You can run a single test with:
# ./run_tests.sh tests/test_zmake.py::TestFilters::test_filter_normal
# Or debug a test with:
# ./run_tests.sh -- --pdb tests/test_zmake.py::TestFilters::test_filter_normal
# Run with coverage
# ./run_tests.sh --coverage

# Exit if any command exits non-zero.
set -e

TEMP=$(getopt -o ch --long coverage,help -n 'run_tests.sh' -- "$@")
eval set -- "${TEMP}"

COVERAGE=false
HELP="run_tests.sh [ -c | --coverage ] [ -h | --help ] pytest_args"
while true; do
  case "$1" in
    -h | --help ) echo "${HELP}" ; exit 0 ;;
    -c | --coverage ) COVERAGE=true; shift ;;
    -- ) shift; break ;;
    * ) break ;;
  esac
done

PYTHON=python3
if which vpython3; then
  PYTHON="vpython3"
fi

# Show commands being run.
set -x

# cd to the directory containing this script.
cd "$(dirname "$(realpath -e "${BASH_SOURCE[0]}")")"

# Test the copy in-tree, instead of what setuptools or the ebuild
# installed.
export PYTHONPATH="${PWD}"

# Run pytest.
if "${COVERAGE}" ; then
  ${PYTHON?} -m coverage erase
  ${PYTHON?} -m coverage run --source=zmake -m pytest -v "$@"
  ${PYTHON?} -m coverage report
  ${PYTHON?} -m coverage html
else
  ${PYTHON?} -m pytest -v "$@"
fi

# Check auto-generated README.md is as expected.
${PYTHON?} -m zmake generate-readme --diff
