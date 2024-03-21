# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if((${BOARD_DIR} MATCHES "boards\/native") OR ("${BOARD}" STREQUAL "unit_testing"))
  set(CROSS_COMPILE_TARGET x86_64-pc-linux-gnu)
endif()

set(CC gcc)
set(CROSS_COMPILE "/usr/bin/${CROSS_COMPILE_TARGET}-")

set(COMPILER gcc)
set(LINKER ld)
set(BINTOOLS gnu)

set(TOOLCHAIN_HAS_NEWLIB OFF CACHE BOOL "True if toolchain supports newlib")

message(STATUS "Found toolchain: host ${ARCH} (gcc/ld)")
