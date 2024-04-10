#!/bin/bash
#
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

mapfile -d '' cmakes < <(find zephyr \( -path zephyr/test -prune \) -o \
  \( -name CMakeLists.txt -o -name tcpmv2.cmake \) -print0)

exit_code=0

# Some tests were mainly copy paste to Zephyr. Add a warning to make sure
# fixes are applied for both versions.
migrated_tests="
test/abort.c
test/aes.cc
test/benchmark.cc
test/boringssl_crypto.cc
test/cortexm_fpu.c
test/crc.c
test/exception.cc
test/flash_write_protect.c
test/fp_transport.c
test/fpsensor_auth_crypto_stateful.cc
test/ftrapv.c
test/libc_printf.c
test/malloc.c
test/panic.c
test/printf.c
test/queue.c
test/restricted_console.c
test/rollback.c
test/rollback_entropy.c
test/sha256.c
test/static_if.c
test/stdlib.c
test/system_is_locked.c
test/utils_str.c
test/utils.c"

for file in "$@"; do
  ec_file="${file##**/platform/ec/}"

  if [[ ${migrated_tests} == *"${file}"* ]]; then
    echo -n "WARNING: ${ec_file} is not used in Zephyr EC. The test "
    echo -n "has been migrated to Zephyr. Make sure you apply the "
    echo "same fix for the Zephyr version in zephyr/test directory."
    exit_code=1
    continue
  fi

  case "${ec_file}" in
    baseboard/*|board/*|chip/*|driver/fingerprint/*|*fpsensor*|test/*|\
    util/*|zephyr/*|extra/*) ;;
    **.c)
      if ! grep -q -F "\${PLATFORM_EC}/${ec_file}" "${cmakes[@]}" ; then
        echo -n "WARNING: ${ec_file} is not used in Zephyr EC. There is "
        echo -n "probably another file in the zephyr/shim directory that "
        echo -n "needs the same fix that you just made. Alternativly, you "
        echo "might have edited the wrong file."
        exit_code=1
      fi
      ;;
  esac
done

exit "${exit_code}"
