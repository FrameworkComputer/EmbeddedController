# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(CMAKE_BUILD_TYPE Release)

# TODO(b/273639386): Remove these workarounds when the upstream supports
# better way to disable the filesystem, threads and locks usages.
set(CMAKE_SYSTEM_NAME Linux)

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CROS_EC_REPO CROSS_COMPILE CC_NAME CXX_NAME)
include("${CROS_EC_REPO}/cmake/toolchain-common.cmake")

# Pretend as "Trusty", an embedded platform.
# TODO(b/273639386): Remove these workarounds when the upstream supports
# better way to disable the filesystem, threads and locks usages.
add_definitions(-D__TRUSTY__)
set(ANDROID TRUE)

# TODO(b/287661706): This can be removed once https://crrev.com/c/4610318 lands.
if (CMAKE_SYSTEM_PROCESSOR STREQUAL armv7)
    add_compile_options(-mcpu=cortex-m4)
    add_compile_options(-mfloat-abi=hard)
endif ()
