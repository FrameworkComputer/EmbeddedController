# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include("${ZEPHYR_BASE}/cmake/compiler/clang/target.cmake")

# TODO(b/407786163): Fix the path reported by "--print-libgcc-file-name" so that
# this is not necessary.
if("${ARCH}" STREQUAL "riscv")
  cmake_path(SET RTLIB_DIR NORMALIZE "${RTLIB_DIR}/../baremetal")
  set_property(TARGET linker PROPERTY lib_include_dir "-L${RTLIB_DIR}")
  set_property(TARGET linker PROPERTY rt_library "-lclang_rt.builtins-riscv32")
endif()

set(CMAKE_C_COMPILER "${CROSS_COMPILE}clang")
set(CMAKE_CXX_COMPILER "${CROSS_COMPILE}clang++")
