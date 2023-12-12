# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if(EXISTS /etc/cros_chroot_version)
  set(CMAKE_C_COMPILER "/usr/bin/x86_64-pc-linux-gnu-clang")
  set(CMAKE_GCOV "/usr/bin/llvm-cov gcov")
else()
  find_program(CMAKE_C_COMPILER clang)
  find_program(CMAKE_GCOV gcov)
endif()
