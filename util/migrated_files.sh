#!/bin/bash
#
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

mapfile -d '' cmakes < <(find zephyr \( -path zephyr/test -prune \) -o \
  -name CMakeLists.txt -print0)

exit_code=0

for file in "$@"; do
  ec_file="${file##**/platform/ec/}"
  case "${ec_file}" in
    baseboard/*|board/*|chip/*|common/fpsensor/*|test/*|util/*|zephyr/*) ;;
    **.c)
      if ! grep -q -F "\${PLATFORM_EC}/${ec_file}" "${cmakes[@]}" ; then
        echo -n "WARNING: ${ec_file} is not used in Zephyr EC. Do not edit this"
        echo -n " unless you also find the zephyr copy and fix the same code"
        echo " there."
        exit_code=1
      fi
      ;;
  esac
done

exit "${exit_code}"
