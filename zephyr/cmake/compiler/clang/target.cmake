# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include("${ZEPHYR_BASE}/cmake/compiler/clang/target.cmake")

set(CMAKE_C_COMPILER "${CROSS_COMPILE}clang")
set(CMAKE_CXX_COMPILER "${CROSS_COMPILE}clang++")
