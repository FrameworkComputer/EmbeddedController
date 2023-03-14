# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(PREFIX armv7m-cros-eabi-)
set(CMAKE_SYSROOT /usr/armv7m-cros-eabi/)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-common.cmake")

add_link_options(-lclang_rt.builtins-armv7m)
add_link_options(-lnosys)
