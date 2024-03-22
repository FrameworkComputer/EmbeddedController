# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Use generic bintools.
include("${TOOLCHAIN_ROOT}/cmake/bintools/llvm/generic.cmake")

if("${ARCH}" STREQUAL "arm")
  set(CMAKE_AR         "${CROSS_COMPILE}ar")
  set(CMAKE_NM         "${CROSS_COMPILE}nm")
  set(CMAKE_OBJCOPY    "${CROSS_COMPILE}objcopy")
  set(CMAKE_OBJDUMP    "${CROSS_COMPILE}objdump")
  set(CMAKE_RANLIB     "${CROSS_COMPILE}ranlib")
  set(CMAKE_READELF    "${CROSS_COMPILE}readelf")

  # CMake is looking for bintools by adding a suffix to compiler binary
  # e.g for AR it would be armv7m-cros-eabi-clang-ar, which doesn't exist.
  # Set bintools locations manually
  set(CMAKE_C_COMPILER_AR         "${CROSS_COMPILE}ar")
  set(CMAKE_C_COMPILER_NM         "${CROSS_COMPILE}nm")
  set(CMAKE_C_COMPILER_OBJCOPY    "${CROSS_COMPILE}objcopy")
  set(CMAKE_C_COMPILER_OBJDUMP    "${CROSS_COMPILE}objdump")
  set(CMAKE_C_COMPILER_RANLIB     "${CROSS_COMPILE}ranlib")
  set(CMAKE_C_COMPILER_READELF    "${CROSS_COMPILE}readelf")

  # And for C++
  set(CMAKE_CXX_COMPILER_AR         "${CROSS_COMPILE}ar")
  set(CMAKE_CXX_COMPILER_NM         "${CROSS_COMPILE}nm")
  set(CMAKE_CXX_COMPILER_OBJCOPY    "${CROSS_COMPILE}objcopy")
  set(CMAKE_CXX_COMPILER_OBJDUMP    "${CROSS_COMPILE}objdump")
  set(CMAKE_CXX_COMPILER_RANLIB     "${CROSS_COMPILE}ranlib")
  set(CMAKE_CXX_COMPILER_READELF    "${CROSS_COMPILE}readelf")

endif()

# Include the GNU bintools properties as a base.
include("${ZEPHYR_BASE}/cmake/bintools/gnu/target_bintools.cmake")
