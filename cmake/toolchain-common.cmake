# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Input variables:
#   CROSS_COMPILE: the prefix of the cross compiler.
#   CC_NAME: the name of the C compiler.
#   CXX_NAME: the name of the C++ compiler.

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER   "${CROSS_COMPILE}${CC_NAME}")
set(CMAKE_CXX_COMPILER "${CROSS_COMPILE}${CXX_NAME}")
set(CMAKE_OBJCOPY      "${CROSS_COMPILE}objcopy")
set(CMAKE_OBJDUMP      "${CROSS_COMPILE}objdump")
set(CMAKE_READELF      "${CROSS_COMPILE}readelf")

if ("${CC_NAME}" STREQUAL "gcc")
set(CMAKE_LINKER       "${CROSS_COMPILE}ld")
else()
set(CMAKE_LINKER       "${CROSS_COMPILE}ld.lld")
endif()

# This is only for host unittests build.
if ("${CC_NAME}" STREQUAL gcc AND "${CMAKE_SYSROOT}" STREQUAL "")
set(CMAKE_AR           "${CROSS_COMPILE}gcc-ar")
set(CMAKE_NM           "${CROSS_COMPILE}gcc-nm")
set(CMAKE_RANLIB       "${CROSS_COMPILE}gcc-ranlib")
else()
set(CMAKE_AR           "${CROSS_COMPILE}ar")
set(CMAKE_NM           "${CROSS_COMPILE}nm")
set(CMAKE_RANLIB       "${CROSS_COMPILE}ranlib")
endif()

if ("${CC_NAME}" STREQUAL gcc)
add_compile_options(-Os)
else()
add_compile_options(-Oz)
endif()

# Enable Link Time Optimization.
add_compile_options(-flto)
add_link_options(-flto)

# See https://www.chromium.org/chromium-os/build/c-exception-support
add_compile_options(-fno-exceptions)
add_compile_options(-fno-unwind-tables)
add_compile_options(-fno-asynchronous-unwind-tables)

set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
