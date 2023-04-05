# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(app PRIVATE src/nx20p348x.c)
target_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC_NX20P3483 app PRIVATE src/nx20p3483.c)
