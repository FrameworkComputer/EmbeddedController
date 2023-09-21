# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(COMPILER clang)
set(LINKER   lld)
set(BINTOOLS llvm)

if("${ARCH}" STREQUAL "arm")
  # LLVM based toolchains for ARM use newlib as a libc.
  set(TOOLCHAIN_HAS_NEWLIB ON CACHE BOOL "True if toolchain supports newlib")
endif()
