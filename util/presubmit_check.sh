#!/bin/bash
#
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ ! -e .tests-passed ]]; then
  echo 'Unit tests have not passed. Please run "make buildall -j".'
  exit 1
fi

changed=$(find ${PRESUBMIT_FILES} -newer .tests-passed)
ec3po_files=$(echo "${PRESUBMIT_FILES}" | grep util/ec3po/)
# Filter out ec3po files from changed files.
changed=$(echo "${changed}" | grep -v util/ec3po/)
if [[ -n "${changed}" ]]; then
  echo "Files have changed since last time unit tests passed:"
  echo "${changed}" | sed -e 's/^/  /'
  echo 'Please run "make buildall -j".'
  exit 1
fi

if [[ ! -e util/ec3po/.tests-passed ]] && [[ -n "${ec3po_files}" ]]; then
  echo 'Unit tests have not passed.  Please run "util/ec3po/run_tests.sh".'
  exit 1
fi

changed_ec3po_files=$(find ${ec3po_files} -newer util/ec3po/.tests-passed)
if [[ -n "${changed_ec3po_files}" ]] && [[ -n "${ec3po_files}" ]]; then
  echo "Files have changed since last time EC-3PO unit tests passed:"
  echo "${changed_ec3po_files}" | sed -e 's/^/  /'
  echo 'Please run "util/ec3po/run_tests.sh".'
  exit 1
fi
