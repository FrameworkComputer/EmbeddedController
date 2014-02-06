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
if [[ -n "${changed}" ]]; then
  echo "Files have changed since last time unit tests passed:"
  echo "${changed}" | sed -e 's/^/  /'
  echo 'Please run "make buildall -j".'
  exit 1
fi
