# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if(TOOLCHAIN_VARIANT_COMPILER STREQUAL "gnu" OR
  NOT DEFINED TOOLCHAIN_VARIANT_COMPILER)

  include("${ZEPHYR_BASE}/cmake/toolchain/host/target.cmake")
elseif(TOOLCHAIN_VARIANT_COMPILER STREQUAL "llvm")
  include("${TOOLCHAIN_ROOT}/cmake/toolchain/host/llvm/target.cmake")
else()
  message(FATAL_ERROR "Unsupported TOOLCHAIN_VARIANT_COMPILER: ${TOOLCHAIN_VARIANT_COMPILER}")
endif()
