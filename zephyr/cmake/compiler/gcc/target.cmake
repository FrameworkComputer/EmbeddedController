# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Zephyr cmake system looks into ${TOOLCHAIN_ROOT}, but we just send
# this out to the copy in ${ZEPHYR_BASE}.
include("${ZEPHYR_BASE}/cmake/compiler/gcc/target.cmake")

# no_libgcc support has been removed in upstream zephyr, but we still
# depend on it.  This ugly hack emulates what it used to do by undoing
# what some of target.cmake does.
if(no_libgcc)
  list(REMOVE_ITEM TOOLCHAIN_LIBS gcc)
endif()
