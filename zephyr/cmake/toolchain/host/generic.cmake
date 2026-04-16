# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# The host toolchain is now used for both GCC and LLVM by specifying either
# ZEPHYR_TOOLCHAIN_VARIANT=host/gnu or ZEPHYR_TOOLCHAIN_VARIANT=host/llvm.
# TODO: move the generic.cmake contenet here and drop toolchain/llvm once zmake
# does not use it anymore.

if(TOOLCHAIN_VARIANT_COMPILER STREQUAL "gnu" OR
  NOT DEFINED TOOLCHAIN_VARIANT_COMPILER)

  set(TOOLCHAIN_KCONFIG_DIR ${TOOLCHAIN_ROOT}/cmake/toolchain/host/gnu)

  if((${BOARD_DIR} MATCHES "boards\/native") OR ("${BOARD}" STREQUAL "unit_testing"))
    set(CROSS_COMPILE_TARGET x86_64-pc-linux-gnu)
  endif()

  set(CC gcc)
  set(CROSS_COMPILE "/usr/bin/${CROSS_COMPILE_TARGET}-")

  set(COMPILER gcc)
  set(LINKER ld)
  set(BINTOOLS gnu)

  set(TOOLCHAIN_HAS_NEWLIB OFF CACHE BOOL "True if toolchain supports newlib")

  set(TOOLCHAIN_VARIANT_COMPILER gnu CACHE STRING "Variant compiler being used")

  message(STATUS "Found toolchain: host ${ARCH} (gcc/ld)")
elseif(TOOLCHAIN_VARIANT_COMPILER STREQUAL "llvm")
  set(TOOLCHAIN_KCONFIG_DIR ${TOOLCHAIN_ROOT}/cmake/toolchain/host/llvm)
  include("${TOOLCHAIN_ROOT}/cmake/toolchain/host/llvm/generic.cmake")
else()
  message(FATAL_ERROR "Unsupported TOOLCHAIN_VARIANT_COMPILER: ${TOOLCHAIN_VARIANT_COMPILER}")
endif()
