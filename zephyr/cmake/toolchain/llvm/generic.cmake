# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(COMPILER clang)
set(LINKER lld)
set(BINTOOLS llvm)

if("${ARCH}" STREQUAL "posix")
set(LINKER ld)
endif()

set(TOOLCHAIN_HAS_LIBCXX ON CACHE BOOL "True if toolchain supports libc++")

set(TOOLCHAIN_VARIANT_COMPILER llvm CACHE STRING "Variant compiler being used")
