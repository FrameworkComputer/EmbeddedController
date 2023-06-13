# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CROSS_COMPILE armv7m-cros-eabi-)
set(CMAKE_SYSROOT /usr/armv7m-cros-eabi/)

set(CC_NAME clang)
set(CXX_NAME clang++)

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CROSS_COMPILE CC_NAME CXX_NAME)
include("${CMAKE_CURRENT_LIST_DIR}/toolchain-common.cmake")

add_compile_options(-mcpu=cortex-m4)
add_compile_options(-mfloat-abi=hard)

add_link_options(-lclang_rt.builtins-armv7m)
add_link_options(-lnosys)
