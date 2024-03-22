# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# generic.cmake is used for host-side compilation and preprocessing
# (e.g., for device-tree).  Thus, we should use LLVM for this
# actually, as that's what's currently supported compiler-wise in the
# chroot right now.
include("${TOOLCHAIN_ROOT}/cmake/toolchain/llvm/generic.cmake")

set(TOOLCHAIN_HAS_PICOLIBC ON CACHE BOOL "True if toolchain supports picolibc")
