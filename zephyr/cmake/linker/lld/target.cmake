# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Include definitions for bfd as a base.  We need to pretend that
# LINKER=ld to do this.
set(LINKER ld)
include("${ZEPHYR_BASE}/cmake/linker/ld/target.cmake")
set(LINKER lld)

# Override the path to the linker.
set(CMAKE_LINKER "${CROSS_COMPILE}ld.lld")

# Zephyr CMake system expects this macro to be defined to provide
# default linker flags.
macro(toolchain_ld_base)
  # For linker scripts, we pretend to bfd-like
  set_property(GLOBAL PROPERTY PROPERTY_LINKER_SCRIPT_DEFINES
    -D__GCC_LINKER_CMD__)

  # Default flags
  zephyr_ld_options(
    ${TOOLCHAIN_LD_FLAGS}
    -Wl,--gc-sections
    --build-id=none)
endmacro()
