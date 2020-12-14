# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Use generic bintools.
include("${TOOLCHAIN_ROOT}/cmake/bintools/llvm/generic.cmake")

# Include the GNU bintools properties as a base.
include("${ZEPHYR_BASE}/cmake/bintools/gnu/target_bintools.cmake")
