# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Include definitions for bfd as a base.
include("${ZEPHYR_BASE}/cmake/linker/ld/linker_flags.cmake")
# ld/linker_flags.cmake includes ${LINKER}/${COMPILER}/linker_flags.cmake but
# that doesn't exist for ldd, so import the path that actually exists.
include("${ZEPHYR_BASE}/cmake/linker/ld/${COMPILER}/linker_flags.cmake" OPTIONAL)

# Add the -nopie option to the linker flags to match the compilation options
set_property(TARGET linker PROPERTY no_position_independent "-nopie")
