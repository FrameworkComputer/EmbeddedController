# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(CMAKE_BUILD_TYPE Release)

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CROS_EC_REPO CROSS_COMPILE CC_NAME CXX_NAME)
include("${CROS_EC_REPO}/cmake/toolchain-common.cmake")

# Specify our platform, which disables filesystem, threads, etc.
add_definitions(-DCROS_EC)

# TODO(b/287661706): This can be removed once https://crrev.com/c/4610318 lands.
if (CMAKE_SYSTEM_PROCESSOR STREQUAL armv7)
    add_compile_options(-mcpu=cortex-m4)
    add_compile_options(-mfloat-abi=hard)
endif ()

# When compiling the code with the portage build system, it will generate very
# long file path strings. This compile options will strip the source path, and
# recude the final code size.
# The reason we don't use "-ffile-prefix-map" here is because we don't want to
# break the debug symbols for debugging.
add_compile_options(-fmacro-prefix-map="${CMAKE_CURRENT_SOURCE_DIR}/=")
