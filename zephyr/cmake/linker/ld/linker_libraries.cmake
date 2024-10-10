# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Zephyr cmake system looks into ${TOOLCHAIN_ROOT}, but we just send
# this out to the copy in ${ZEPHYR_BASE}.
include("${ZEPHYR_BASE}/cmake/linker/ld/linker_libraries.cmake")

if(NOT CONFIG_NATIVE_BUILD)
  # In general we don't want libc.
  message(WARNING "Disabling c_library")
  set_linker_property(PROPERTY c_library "")
endif()
