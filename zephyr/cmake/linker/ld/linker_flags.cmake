# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Zephyr cmake system looks into ${TOOLCHAIN_ROOT}, but we just send
# this out to the copy in ${ZEPHYR_BASE}.
include("${ZEPHYR_BASE}/cmake/linker/ld/linker_flags.cmake")

# There can also be compiler specific linker options, so try to include
# our version of that also.
include("${TOOLCHAIN_ROOT}/cmake/linker/${LINKER}/${COMPILER}/linker_flags.cmake" OPTIONAL)

# TODO(b/374997019): Remove this and make the sections not RWX.
add_link_options("${LINKERFLAGPREFIX},--no-warn-rwx-segments")
