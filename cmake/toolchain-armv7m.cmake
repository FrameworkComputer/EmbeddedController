# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(PREFIX armv7m-cros-eabi-)
set(CMAKE_SYSROOT /usr/armv7m-cros-eabi/)

set(CMAKE_C_COMPILER ${PREFIX}clang)
set(CMAKE_CXX_COMPILER ${PREFIX}clang++)
set(CMAKE_LINKER ${PREFIX}ld.lld)
set(CMAKE_AR ${PREFIX}ar)
set(CMAKE_NM ${PREFIX}nm)
set(CMAKE_OBJCOPY ${PREFIX}objcopy)
set(CMAKE_OBJDUMP ${PREFIX}objdump)
set(CMAKE_RANLIB ${PREFIX}ranlib)
set(CMAKE_READELF ${PREFIX}readelf)

add_compile_options(-Oz)

add_compile_options(-flto)
add_link_options(-flto)

add_link_options(-lclang_rt.builtins-armv7m)
add_link_options(-lnosys)

set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
