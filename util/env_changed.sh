#!/bin/bash
# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script accepts two parameters: a file name and a string.
#
# If the file does not exist, or does not contain the passed in string wrapped
# in '/*... */, the file is written with the wrapped passed in string.

h_file="$1"
current_set="/* $2 */"

if [[ -f "${h_file}" ]]; then
  old_set="$(cat "${h_file}")"
  if [[ "${current_set}" == "${old_set}" ]]; then
    exit 0
  fi
else
  dest_dir="$(dirname "${h_file}")"
  [[ -d "${dest_dir}" ]] || mkdir -p "${dest_dir}"
fi
printf "%s" "${current_set}" > "${h_file}"
