# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(CROSS_COMPILE_TARGET_posix        x86_64-pc-linux-gnu)
set(CROSS_COMPILE_TARGET_unit_testing x86_64-pc-linux-gnu)
set(CROSS_COMPILE_TARGET          ${CROSS_COMPILE_TARGET_${ARCH}})

set(CC gcc)
set(CROSS_COMPILE "/usr/bin/${CROSS_COMPILE_TARGET}-")

set(COMPILER gcc)
set(LINKER ld)
set(BINTOOLS gnu)

set(TOOLCHAIN_HAS_NEWLIB OFF CACHE BOOL "True if toolchain supports newlib")

message(STATUS "Found toolchain: host ${ARCH} (gcc/ld)")
